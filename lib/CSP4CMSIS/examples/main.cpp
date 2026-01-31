#include "csp/csp4cmsis.h"
#include "cmsis_os2.h"  // CMSIS-RTOS2 header for Nucleo/Keil
#include <cstdio>
#include <vector>

using namespace csp;

// --- Configuration ---
#define NUM_RELAYS 5
#define TEST_ITERATIONS 1000
#define CHECK_INTERVAL 100

// --- 1. Define the Sequential Processes ---

class CountingSender : public CSProcess {
private:
    Chanout<int> out;
public:
    CountingSender(Chanout<int> w) : out(w) {}

    void run() override {
        printf("[Sender] Starting stream...\r\n");
        for (int i = 1; i <= TEST_ITERATIONS; ++i) {
            out << i;
        }
        printf("[Sender] Stream complete.\r\n");
        
        // CMSIS-RTOS2 version of portMAX_DELAY
        osDelay(osWaitForever); 
    }
};

class Relay : public CSProcess {
private:
    Chanin<int> in;
    Chanout<int> out;
    int id;
public:
    Relay(Chanin<int> r, Chanout<int> w, int relay_id) 
        : in(r), out(w), id(relay_id) {}

    void run() override {
        int data;
        while (true) {
            in >> data;
            out << data;
        }
    }
};

class CheckerReceiver : public CSProcess {
private:
    Chanin<int> in;
public:
    CheckerReceiver(Chanin<int> r) : in(r) {}

    void run() override {
        int received;
        bool success = true;
        for (int i = 1; i <= TEST_ITERATIONS; ++i) {
            in >> received;
            if (received != i) {
                printf("[Receiver] !! DATA ERROR: Expected %d, Got %d\r\n", i, received);
                success = false;
                break;
            }
            if (i % CHECK_INTERVAL == 0) {
                printf("[Receiver] Verified up to %d...\r\n", i);
            }
        }
        if (success) {
            printf("[Receiver] SUCCESS: All %d values verified through %d relays.\r\n", 
                   TEST_ITERATIONS, NUM_RELAYS);
        }
        osDelay(osWaitForever); 
    }
};

// --- 2. Network Construction ---

void MainApp_Task(void* params) {
    // 500ms delay to allow UART/Serial to stabilize
    osDelay(500); 
    printf("\r\n--- Launching CSP Relay Chain (Nucleo/CMSIS-RTOS2) ---\r\n");

    static Channel<int> channels[NUM_RELAYS + 1];

    static CountingSender sender(channels[0].writer());
    static CheckerReceiver receiver(channels[NUM_RELAYS].reader());

    static Relay relays[NUM_RELAYS] = {
        Relay(channels[0].reader(), channels[1].writer(), 0),
        Relay(channels[1].reader(), channels[2].writer(), 1),
        Relay(channels[2].reader(), channels[3].writer(), 2),
        Relay(channels[3].reader(), channels[4].writer(), 3),
        Relay(channels[4].reader(), channels[5].writer(), 4)
    };

    Run(
        InParallel(sender, relays[0], relays[1], relays[2], relays[3], relays[4], receiver),
        ExecutionMode::StaticNetwork
    );
}

// --- 3. Main Entry Point for Keil Studio ---

int main(void) {
    // Initialize the kernel
    osKernelInitialize();

    // Create the MainApp thread
    // Stack size: 4096 bytes, Priority: Above Normal
    const osThreadAttr_t attr = {
        .name = "MainApp",
        .stack_size = 4096,
        .priority = osPriorityAboveNormal
    };
    
    osThreadNew(MainApp_Task, NULL, &attr);

    // Start the RTOS
    osKernelStart();

    for (;;) {}
}
