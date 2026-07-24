#include "application.h"

#include "csp/csp4cmsis.h"
#include <cstdio>

// --- Configuration ---
#define TOTAL_MESSAGES_PER_SENDER 1000000 // Reduced for clarity in terminal output
#define CHECK_INTERVAL 10000
#define MAX_TOTAL_MESSAGES (TOTAL_MESSAGES_PER_SENDER * 2)

// How often to print a stack-usage report for the whole network. This is
// a *live* reading (CSProcess::stackHighWaterMarkWords()), not a one-shot
// end-of-run report, because Sender/Receiver below run forever. See
// freertos-stack-usage-estimation-brief.md for what the number means and
// its caveats: it's a measured worst-observed-so-far, not a proven bound,
// and is only trustworthy once the deepest call path has actually been
// exercised.
#ifndef CSP_STACK_REPORT_INTERVAL_MS
#define CSP_STACK_REPORT_INTERVAL_MS (3000)
#endif

// MainApp_Task isn't a CSProcess, so it doesn't get a static stack/TCB
// "for free" from CSProcessStatic<N> -- it is still spawned with a plain
// (dynamic) xTaskCreate() here, but we keep its handle so the report loop
// below can measure its own headroom too, same live-HWM approach as
// CSProcess::stackHighWaterMarkWords(), just without a CSProcess object
// to hang it off of.
#define MAIN_APP_STACK_WORDS 2048

using namespace csp;

struct Message {
    int source_id;
    int sequence_num;
};

// --- 1. Define Channels ---
// 'Channel' or 'One2OneChannel' now represents a Rendezvous (capacity 0) sync point.
using AltChannel = Channel<Message>;

// --- 2. Define Processes ---
// API 1.3: CSP4CMSIS makes no dynamic (heap) allocations for task
// creation -- every CSProcess supplies its own stack buffer and TCB as
// static storage via CSProcessStatic<N>, so the stack depth is a fixed,
// compile-time property of the process (queried by Run()/ParallelHelper
// to build the xTaskCreateStatic() call, and by
// stackHighWaterMarkWords() below to report headroom).
class Sender : public CSProcessStatic<256> {
private:
    Chanout<Message> out;
    int id;
public:
    Sender(Chanout<Message> w, int sender_id) : out(w), id(sender_id) {}

    const char* name() const override { return "Sender"; }

    void run() override {
        printf("[Sender %d] Starting sequence.\r\n", id);
        for (int i = 0; i < TOTAL_MESSAGES_PER_SENDER; ++i) {
            Message msg = {id, i};
            out << msg;
        }
        printf("[Sender %d] Finished.\r\n", id);
        while (true) {
            vTaskDelay(portMAX_DELAY);
        }
    }
};

class Receiver : public CSProcessStatic<512> {
private:
    Chanin<Message> inA;
    Chanin<Message> inB;
public:
    Receiver(Chanin<Message> rA, Chanin<Message> rB) : inA(rA), inB(rB) {}

    const char* name() const override { return "Receiver"; }

    void run() override {
        vTaskDelay(pdMS_TO_TICKS(10));
        printf("[Receiver] Task running. Using Resident-Guard ALT.\r\n");

        Message msgA, msgB;
        int count = 0;
        int next_seqA = 0;
        int next_seqB = 0;
        bool error_found = false;

        // The Alternative object is on the stack.
        // It borrows pointers to guards that live inside chan_A and chan_B.
        Alternative alt(inA | msgA, inB | msgB);

        while(count < MAX_TOTAL_MESSAGES) {
            // fairSelect is now heap-free.
            int selected = alt.fairSelect();

            if (selected == 0) {
                if (msgA.source_id != 1 || msgA.sequence_num != next_seqA) {
                    printf("!! DATA ERROR Chan A: Expected ID 1 Seq %d, Got ID %d Seq %d\r\n",
                            next_seqA, msgA.source_id, msgA.sequence_num);
                    error_found = true;
                }
                next_seqA++;
            }
            else if (selected == 1) {
                if (msgB.source_id != 2 || msgB.sequence_num != next_seqB) {
                    printf("!! DATA ERROR Chan B: Expected ID 2 Seq %d, Got ID %d Seq %d\r\n",
                            next_seqB, msgB.source_id, msgB.sequence_num);
                    error_found = true;
                }
                next_seqB++;
            }

            count++;
            if (count % CHECK_INTERVAL == 0) {
                printf("[Receiver] Verified %d messages...\r\n", count);
                if (error_found) break;
            }
        }

        if (!error_found) {
            printf("[Receiver] SUCCESS: %d messages verified heap-free.\r\n", count);
        }
        while (true) {
            vTaskDelay(portMAX_DELAY);
        }
    }
};

// Set by csp_app_main_init() right after xTaskCreate() succeeds, so the
// report loop below can measure MainApp_Task's own headroom too.
static TaskHandle_t s_main_app_task_handle = NULL;

// --- 3. The Main Application Task ---
void MainApp_Task(void* params) {
    vTaskDelay(pdMS_TO_TICKS(10));

    printf("\r\n--- BOli2 Launching CSP Static Network (Zero-Heap) ---\r\n");

    // NEW: No constructor arguments needed for Rendezvous.
    // These are placed in static memory (.data segment).
    static AltChannel chan_A;
    static AltChannel chan_B;

    static Sender sA(chan_A.writer(), 1);
    static Sender sB(chan_B.writer(), 2);
    static Receiver r1(chan_A.reader(), chan_B.reader());

    // Run parallel processes using static execution.
    // Kept as a named object, rather than an InParallel(...) temporary
    // passed straight into Run(), so we can keep using it below --
    // ParallelHelper still holds references to sA/sB/r1 after Run()
    // returns, which forEachProcess() needs for the report loop.
    auto network = InParallel(sA, sB, r1);
    Run(network, ExecutionMode::StaticNetwork);

    printf("*** MainApp_Task: Run() returned, entering report loop ***\r\n");

    // sA/sB/r1 never return from run(), so there's no "network finished"
    // point to report stack usage at -- report periodically instead.
    while (true) {
        vTaskDelay(pdMS_TO_TICKS(CSP_STACK_REPORT_INTERVAL_MS));

        if (s_main_app_task_handle != NULL) {
            UBaseType_t hwm = uxTaskGetStackHighWaterMark(s_main_app_task_handle);
            size_t unused_bytes = hwm * sizeof(StackType_t);
            printf("MainApp_Task: %u bytes unused headroom (%u words HWM, of %u allocated)\r\n",
                    (unsigned)unused_bytes, (unsigned)hwm, (unsigned)MAIN_APP_STACK_WORDS);
        }

        network.forEachProcess([](CSProcess& p) {
            // API 1.3: stack depth is fixed at compile time -- no more
            // resolveStackWords()/fallback constant to consult, just ask
            // the process directly.
            size_t allocated_words = p.stackWords();
            size_t allocated_bytes = allocated_words * sizeof(StackType_t);

            UBaseType_t hwm = p.stackHighWaterMarkWords();
            if (hwm == CSP_STACK_HWM_UNAVAILABLE) {
                printf("%s: allocated = %u words (%u bytes), HWM unavailable\r\n",
                        p.name(), (unsigned)allocated_words, (unsigned)allocated_bytes);
            } else {
                size_t unused_bytes = hwm * sizeof(StackType_t);
                size_t used_bytes = (unused_bytes <= allocated_bytes)
                                        ? allocated_bytes - unused_bytes
                                        : 0; // guard against any inconsistency
                printf("%s: %u/%u bytes used (%u bytes unused headroom, %u words HWM)\r\n",
                        p.name(), (unsigned)used_bytes, (unsigned)allocated_bytes,
                        (unsigned)unused_bytes, (unsigned)hwm);
            }
        });
    }
}

void csp_app_main_init(void) {
	BaseType_t status = xTaskCreate(MainApp_Task, "MainApp", MAIN_APP_STACK_WORDS, NULL,
	                                 tskIDLE_PRIORITY + 3, &s_main_app_task_handle);
	if (status != pdPASS) {
	    printf("ERROR: MainApp_Task creation failed!\r\n");
	}
}
