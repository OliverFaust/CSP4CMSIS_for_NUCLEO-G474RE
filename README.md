# CSP4CMSIS Demo for NUCLEO-G474RE

A demonstration of the CSP (Communicating Sequential Processes) library for CMSIS-RTOS v2 on STM32G474RE microcontroller.

## Features
- FreeRTOS with CMSIS-RTOS v2 API
- CSP4CMSIS library for channel-based communication
- Rendezvous synchronization pattern
- Static memory allocation (zero heap usage)
- Serial console output via USART1

## Hardware Requirements
- STM32 Nucleo-G474RE board
- USB cable for programming and serial communication

## Serial Configuration
- Baud Rate: 115200
- Data Bits: 8
- Stop Bits: 1
- Parity: None

## Building with STM32CubeIDE
1. Clone this repository. Any directory will do but not the STM32CubeIDE workspace. 
2. Open STM32CubeIDE
3. File → Import → Existing Projects into Workspace
4. Select this directory
5. Build and flash to your Nucleo board

## Project Structure
- `Core/` - Application source code
- `Drivers/` - STM32 HAL drivers
- `lib/CSP4CMSIS/` - CSP library
- `Middlewares/` - FreeRTOS middleware

## Example Output
```text
Welcome to STM32 world !
=== STM32 FreeRTOS + CSP4CMSIS bootstrap ===
--- BOli2 Launching CSP Static Network (Zero-Heap) ---
[Sender 1] Starting sequence.
[Sender 2] Starting sequence.
[Receiver] Task running. Using Resident-Guard ALT.
[Receiver] Verified 10000 messages...
[Receiver] SUCCESS: 2000000 messages verified heap-free.
```

## License
MIT License

## Acknowledgments
- STMicroelectronics for the HAL library
- FreeRTOS team

