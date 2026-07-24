#!/usr/bin/env python3
"""
csp4cmsis_map_report.py
------------------------
Drop-in analysis of a GNU ld linker .map file (as produced by
STM32CubeIDE / arm-none-eabi-gcc with -Wl,-Map=<project>.map) to show how
much FLASH and RAM the CSP4CMSIS library itself is responsible for,
separate from HAL/BSP/FreeRTOS/user code.

Usage:
    python3 csp4cmsis_map_report.py path/to/project.map
    python3 csp4cmsis_map_report.py project.map --top 30
    python3 csp4cmsis_map_report.py project.map --pattern 'csp|Sender|Receiver'
    python3 csp4cmsis_map_report.py project.map --no-demangle

Requirements:
    - Python 3.7+, stdlib only.
    - Optional: arm-none-eabi-c++filt (or plain c++filt) on PATH for
      demangled C++ symbol names. Falls back to raw mangled names if
      not found.

What it does:
    1. Reads "Memory Configuration" to get FLASH/RAM origin+length.
    2. Walks "Linker script and memory map" and collects every
       (sub)section entry: section name, address, size, contributing
       object file -- plus bare symbol assignments (e.g. _end, _estack,
       _Min_Stack_Size) used by sysmem.c's _sbrk().
    3. Classifies each entry as CSP4CMSIS or "other" using a filename/
       symbol-name pattern (default covers the csp4cmsis core sources:
       alt_channel_sync, alternative, barrier, buffered_channel,
       channel_sync, sync_channel, csp_wrapper, glue, kernel,
       csp4cmsis_spn, application, console_process, camera_process,
       inference_process -- adjust with --pattern for your project).
    4. Prints:
       - FLASH/RAM totals for CSP4CMSIS vs the whole image
       - a per-object-file breakdown
       - the N largest individual CSP4CMSIS symbols
       - the stack/heap headroom implied by _end/_estack/_Min_Stack_Size,
         matching the picture drawn in sysmem.c's _sbrk() comment.
"""

import argparse
import re
import shutil
import subprocess
import sys
from collections import defaultdict

# --- Default set of object-file name fragments considered "CSP4CMSIS".
# Matched case-insensitively against the object file path in each map
# entry. Extend/override with --pattern if your project adds more.
DEFAULT_CSP_FILE_FRAGMENTS = [
    "alt_channel_sync",
    "alternative",
    "barrier",
    "buffered_channel",
    "channel_sync",
    "sync_channel",
    "csp_wrapper",
    "glue",
    "kernel",
    "csp4cmsis_spn",
    "application",
    "console_process",
    "camera_process",
    "inference_process",
]

HEX = r"0x[0-9a-fA-F]+"

# Matches a fully-formed sub-section entry on one line:
#   .text._ZN3csp8internal...   0x08001234   0x28   ./obj/alternative.o
RE_ENTRY_ONE_LINE = re.compile(
    rf"^\s+(?P<section>\.\S+)\s+(?P<addr>{HEX})\s+(?P<size>{HEX})\s+(?P<obj>\S+)\s*$"
)

# Matches a sub-section name alone (entry wraps to the next line because
# the section name is too long):
#   .text._ZN3csp8internal14AltChanSyncBaseC2Ev
RE_SECTION_NAME_ONLY = re.compile(r"^\s+(?P<section>\.\S+)\s*$")

# Matches the continuation line for the above:
#                   0x08001234       0x28 ./obj/alternative.o
RE_ENTRY_CONT = re.compile(
    rf"^\s+(?P<addr>{HEX})\s+(?P<size>{HEX})\s+(?P<obj>\S+)\s*$"
)

# Matches a bare symbol definition/assignment (no size), e.g.:
#                 0x08004520                g_pfnVectors
#                 0x2000ff00                _estack = ORIGIN(RAM) + LENGTH(RAM)
RE_SYMBOL = re.compile(
    rf"^\s+(?P<addr>{HEX})\s+(?P<sym>[A-Za-z_.$][\w.$]*)\b"
)

# Matches "Memory Configuration" region rows:
#   FLASH            0x08000000         0x00080000         xr
RE_MEMCFG_ROW = re.compile(
    rf"^(?P<name>\w+)\s+(?P<origin>{HEX})\s+(?P<length>{HEX})\s+\S*\s*$"
)

TOP_SECTION_KIND = {
    ".text": "FLASH",
    ".isr_vector": "FLASH",
    ".rodata": "FLASH",
    ".ARM": "FLASH",
    ".init_array": "FLASH",
    ".fini_array": "FLASH",
    ".data": "RAM",
    ".bss": "RAM",
    ".noinit": "RAM",
    ".heap": "RAM",
    ".stack": "RAM",
}


def classify_kind(section_name: str) -> str:
    """Map a (sub)section name like '.text._ZN3csp...' to FLASH/RAM/OTHER."""
    for prefix, kind in TOP_SECTION_KIND.items():
        if section_name == prefix or section_name.startswith(prefix + "."):
            return kind
    return "OTHER"


def find_demangler():
    for candidate in ("arm-none-eabi-c++filt", "c++filt"):
        path = shutil.which(candidate)
        if path:
            return path
    return None


def demangle_all(names, demangler_path):
    """Batch-demangle via c++filt (one process, newline-separated stdin)."""
    if not demangler_path or not names:
        return {n: n for n in names}
    try:
        proc = subprocess.run(
            [demangler_path],
            input="\n".join(names),
            capture_output=True,
            text=True,
            timeout=30,
        )
        out_lines = proc.stdout.splitlines()
        if len(out_lines) == len(names):
            return dict(zip(names, out_lines))
    except Exception:
        pass
    return {n: n for n in names}


def parse_memory_configuration(lines):
    regions = {}
    in_section = False
    for line in lines:
        if line.strip() == "Memory Configuration":
            in_section = True
            continue
        if in_section:
            if line.strip().startswith("Linker script and memory map"):
                break
            m = RE_MEMCFG_ROW.match(line)
            if m and m.group("name").lower() != "name":
                regions[m.group("name")] = (
                    int(m.group("origin"), 16),
                    int(m.group("length"), 16),
                )
    return regions


def parse_map(path):
    """Returns (entries, symbols) where:
    entries = list of dicts: section, addr, size, obj
    symbols = list of dicts: addr, name
    """
    with open(path, "r", errors="replace") as f:
        lines = f.readlines()

    regions = parse_memory_configuration(lines)

    entries = []
    symbols = []

    # Find start of "Linker script and memory map"
    start = 0
    for i, line in enumerate(lines):
        if line.strip().startswith("Linker script and memory map"):
            start = i + 1
            break

    pending_section = None
    for line in lines[start:]:
        if line.strip().startswith("OUTPUT(") or line.strip().startswith(
            "Cross Reference Table"
        ):
            break

        m1 = RE_ENTRY_ONE_LINE.match(line)
        if m1:
            entries.append(
                {
                    "section": m1.group("section"),
                    "addr": int(m1.group("addr"), 16),
                    "size": int(m1.group("size"), 16),
                    "obj": m1.group("obj"),
                }
            )
            pending_section = None
            continue

        if pending_section is not None:
            m2 = RE_ENTRY_CONT.match(line)
            if m2:
                entries.append(
                    {
                        "section": pending_section,
                        "addr": int(m2.group("addr"), 16),
                        "size": int(m2.group("size"), 16),
                        "obj": m2.group("obj"),
                    }
                )
                pending_section = None
                continue
            else:
                # continuation didn't match (e.g. it was a symbol line) --
                # fall through and re-check this line normally below.
                pending_section = None

        m3 = RE_SECTION_NAME_ONLY.match(line)
        if m3 and not line.strip().startswith("*("):
            pending_section = m3.group("section")
            continue

        m4 = RE_SYMBOL.match(line)
        if m4:
            symbols.append(
                {"addr": int(m4.group("addr"), 16), "name": m4.group("sym")}
            )
            continue

    return regions, entries, symbols


def is_csp_entry(entry, symbol_names_by_addr, fragments):
    obj = entry["obj"].lower()
    if any(frag.lower() in obj for frag in fragments):
        return True
    # Fall back to symbol-name check (mangled C++ names embed "3csp" for
    # the csp:: namespace) in case the object file name itself doesn't
    # give it away (e.g. unusual build layouts, LTO'd translation units).
    sym = symbol_names_by_addr.get(entry["addr"], "")
    if "3csp" in sym or "csp::" in sym:
        return True
    return False


def human(n):
    return f"{n:,} B"


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("map_file", help="Path to the linker .map file")
    ap.add_argument("--top", type=int, default=25, help="Show N largest CSP4CMSIS symbols (default 25)")
    ap.add_argument(
        "--pattern",
        default=None,
        help="Comma or |-separated list of object-filename fragments that count as "
        "CSP4CMSIS. Overrides the built-in default list.",
    )
    ap.add_argument("--no-demangle", action="store_true", help="Skip c++filt demangling")
    args = ap.parse_args()

    fragments = DEFAULT_CSP_FILE_FRAGMENTS
    if args.pattern:
        fragments = [p for p in re.split(r"[,|]", args.pattern) if p]

    try:
        regions, entries, symbols = parse_map(args.map_file)
    except FileNotFoundError:
        print(f"error: map file not found: {args.map_file}", file=sys.stderr)
        sys.exit(1)

    if not entries:
        print(
            "warning: no per-object section entries were found. This usually means\n"
            "the build wasn't compiled with -ffunction-sections -fdata-sections, so\n"
            "the linker couldn't attribute individual symbols to files. Per-object\n"
            "and per-symbol breakdowns will be empty; only region totals will show.",
            file=sys.stderr,
        )

    symbol_by_addr = {s["addr"]: s["name"] for s in symbols}

    demangler = None if args.no_demangle else find_demangler()
    if not args.no_demangle and demangler is None:
        print("note: c++filt not found on PATH; symbol names will be shown mangled.\n", file=sys.stderr)

    # --- Classify entries ---
    csp_entries = [e for e in entries if is_csp_entry(e, symbol_by_addr, fragments)]
    other_entries = [e for e in entries if e not in csp_entries]

    def totals(entry_list):
        t = defaultdict(int)
        for e in entry_list:
            t[classify_kind(e["section"])] += e["size"]
        return t

    csp_totals = totals(csp_entries)
    other_totals = totals(other_entries)
    all_totals = totals(entries)

    print("=" * 72)
    print("CSP4CMSIS Linker Map Report")
    print("=" * 72)
    print(f"Map file: {args.map_file}")
    print(f"Classifying as CSP4CMSIS by object-file match: {', '.join(fragments)}")
    print()

    # --- Region summary ---
    if regions:
        print("Memory regions:")
        for name, (origin, length) in regions.items():
            print(f"  {name:<8} origin=0x{origin:08X}  length={human(length)}")
        print()

    print("FLASH / RAM footprint (bytes):")
    print(f"{'':16}{'CSP4CMSIS':>14}{'Rest of image':>16}{'Total':>14}")
    for kind in ("FLASH", "RAM"):
        c = csp_totals.get(kind, 0)
        o = other_totals.get(kind, 0)
        t = all_totals.get(kind, 0)
        print(f"{kind:<16}{c:>14,}{o:>16,}{t:>14,}")
        if kind in regions or True:
            region_len = None
            # RAM/FLASH region name assumed to match kind; adjust if your
            # linker script uses different region names.
            for rname, (origin, length) in regions.items():
                if rname.upper() == kind:
                    region_len = length
                    break
            if region_len:
                pct = 100.0 * t / region_len if region_len else 0.0
                pct_csp = 100.0 * c / region_len if region_len else 0.0
                print(
                    f"{'':16}  -> {pct_csp:5.2f}% of {kind} region used by CSP4CMSIS "
                    f"({pct:5.2f}% used total, region = {human(region_len)})"
                )
    print()

    # --- Per-object breakdown ---
    if csp_entries:
        by_obj = defaultdict(lambda: defaultdict(int))
        for e in csp_entries:
            by_obj[e["obj"]][classify_kind(e["section"])] += e["size"]

        print("Per-object-file breakdown (CSP4CMSIS only):")
        print(f"{'Object file':<45}{'FLASH':>12}{'RAM':>12}")
        for obj, kinds in sorted(by_obj.items(), key=lambda kv: -sum(kv[1].values())):
            print(f"{obj:<45}{kinds.get('FLASH', 0):>12,}{kinds.get('RAM', 0):>12,}")
        print()

    # --- Largest individual symbols ---
    if csp_entries:
        print(f"Largest CSP4CMSIS symbols (top {args.top}):")
        sym_names = [symbol_by_addr.get(e["addr"], e["section"]) for e in csp_entries]
        demangled = demangle_all(sorted(set(sym_names)), demangler)

        rows = []
        for e in csp_entries:
            raw = symbol_by_addr.get(e["addr"], e["section"])
            rows.append((e["size"], classify_kind(e["section"]), demangled.get(raw, raw), e["obj"]))
        rows.sort(key=lambda r: -r[0])

        print(f"{'Size':>10}  {'Kind':<6} Symbol / section  (object file)")
        for size, kind, name, obj in rows[: args.top]:
            short_name = name if len(name) <= 80 else name[:77] + "..."
            print(f"{size:>10,}  {kind:<6} {short_name}  ({obj})")
        print()

    # --- Stack/heap headroom, matching sysmem.c's _sbrk() picture ---
    interesting = {"_end", "_estack", "_Min_Stack_Size", "_Min_Heap_Size", "_sdata", "_edata", "_sbss", "_ebss"}
    found = {s["name"]: s["addr"] for s in symbols if s["name"] in interesting}
    if "_end" in found and "_estack" in found:
        end = found["_end"]
        estack = found["_estack"]
        min_stack = found.get("_Min_Stack_Size")  # this is usually a size, not an address symbol
        print("Heap / stack layout (per sysmem.c's _sbrk() picture):")
        print(f"  _end     = 0x{end:08X}   (top of .data+.bss / start of heap)")
        print(f"  _estack  = 0x{estack:08X}   (top of RAM / initial MSP)")
        print(f"  heap+stack region available = {human(estack - end)}")
        print(
            "  (actual usable heap = that region minus the reserved MSP stack, i.e.\n"
            "   _Min_Stack_Size -- see sysmem.c's _sbrk() for the exact check.)"
        )
        print()

    print("=" * 72)
    print("Tip: rebuild with -ffunction-sections -fdata-sections -Wl,--gc-sections")
    print("(STM32CubeIDE does this by default in Release) to get one map entry per")
    print("symbol -- without it, only region totals above are meaningful.")
    print("=" * 72)


if __name__ == "__main__":
    main()
