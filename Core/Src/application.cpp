#include "application.h"

#include "csp/csp4cmsis.h"
#include <cstdio>

// --- Configuration ---
#define TOTAL_MESSAGES_PER_SENDER 1000000 // Reduced for clarity in terminal output
#define CHECK_INTERVAL 10000
#define MAX_TOTAL_MESSAGES (TOTAL_MESSAGES_PER_SENDER * 2)

using namespace csp;

struct Message {
    int source_id;
    int sequence_num;
};

// --- 1. Define Channels ---
// 'Channel' or 'One2OneChannel' now represents a Rendezvous (capacity 0) sync point.
using AltChannel = Channel<Message>;

// --- 2. Define Processes ---
class Sender : public CSProcess {
private:
    Chanout<Message> out;
    int id;
public:
    Sender(Chanout<Message> w, int sender_id) : out(w), id(sender_id) {}

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

class Receiver : public CSProcess {
private:
    Chanin<Message> inA;
    Chanin<Message> inB;
public:
    Receiver(Chanin<Message> rA, Chanin<Message> rB) : inA(rA), inB(rB) {}

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

    // Run parallel processes using static execution
    Run(
        InParallel(sA, sB, r1),
        ExecutionMode::StaticNetwork
    );
}

void csp_app_main_init(void) {
	BaseType_t status = xTaskCreate(MainApp_Task, "MainApp", 2048, NULL, tskIDLE_PRIORITY + 3, NULL);
	if (status != pdPASS) {
	    printf("ERROR: MainApp_Task creation failed!\r\n");
	}
}
