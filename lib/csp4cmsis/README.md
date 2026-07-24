# csp4cmsis

A high-performance, **Zero-Heap**, C++ implementation of **Communicating Sequential Processes (CSP)** tailored for ARM CMSIS-compliant microcontrollers. It is specifically optimized for the **Cortex-M55** and the **Himax WE2** platform.

This library enables embedded developers to move away from complex mutex/semaphore management toward a formal model of "Processes" that communicate via synchronized "Channels," significantly reducing race conditions and concurrency bugs.



---

## 🚀 Core Philosophy

* **Zero Dynamic Memory:** All channels, processes, and synchronization primitives are designed to be allocated statically or on the stack, ensuring no heap fragmentation in safety-critical loops.
* **Rendezvous-Based:** Fundamental synchronization occurs when a sender and receiver meet, ensuring data integrity without the need for intermediate buffering.
* **CMSIS/FreeRTOS Integration:** Native integration with FreeRTOS primitives (Task Notifications, Event Groups) hidden behind a high-level, type-safe C++ API.
* **Compile-Time Safety:** Uses C++ templates to ensure that channel data types are checked at compile-time.

---

## 📂 Library Structure

### `inc/csp/` (Headers)
* **`csp4cmsis.h`**: The primary entry point. Include this in your application.
* **`process.h`**: Defines the `CSProcess` base class for creating concurrent actors.
* **`channel_base.h` / `rendezvous_channel.h`**: Synchronous communication pipes.
* **`alt.h`**: Implements the `Alternative` mechanism for non-deterministic input multiplexing (similar to `select` in Go).
* **`buffered_channel.h` / `overwriting_channel.h`**: Specialized channels for asynchronous or lossy data flow.
* **`barrier.h`**: Multi-process synchronization points.

### `src/` (Implementation)
* **`kernel.cpp`**: The glue between CSP logic and the underlying RTOS scheduler.
* **`alternative.cpp`**: Logic for fair selection and bit-masking for multi-channel monitoring.
* **`sync_channel.cpp`**: Core implementation of the synchronous rendezvous logic.
* **`glue.cpp`**: Internal adapters for CMSIS-compliant RTOS calls.

---

## 🛠 Basic Usage

### 1. Define your Processes
Inherit from `CSProcess` and implement the `run()` method.

```cpp
class Producer : public CSProcess {
    Chanout<int> out;
public:
    Producer(Chanout<int> o) : out(o) {}
    void run() override {
        for(int i = 0; i < 10; ++i) {
            out << i; // Blocks until receiver is ready
        }
    }
};

class Consumer : public CSProcess {
    Chanin<int> in;
public:
    Consumer(Chanin<int> i) : in(i) {}
    void run() override {
        int val;
        while(true) {
            in >> val; // Blocks until sender is ready
            printf("Received: %d\n", val);
        }
    }
};
