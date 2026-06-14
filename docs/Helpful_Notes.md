# ESP32 and FreeRTOS: Comprehensive Architecture & Development Guide

This document outlines core concepts, architectural principles, and best practices for developing robust applications using ESP-IDF and FreeRTOS on the ESP32.

## Table of Contents
1. [Architecture & OS Fundamentals](#1-architecture--os-fundamentals)
2. [Task Management & Separation of Concerns](#2-task-management--separation-of-concerns)
3. [Memory Management & Task Lifecycle](#3-memory-management--task-lifecycle)
4. [Inter-Task Communication & Synchronization](#4-inter-task-communication--synchronization)
5. [Advanced Execution Contexts (Hooks & Critical Sections)](#5-advanced-execution-contexts)
6. [Best Practices & Error Handling](#6-best-practices--error-handling)

---

## 1. Architecture & OS Fundamentals

### Symmetric Multiprocessing (SMP)
In the ESP32 SMP architecture, Core 0 and Core 1 share the same memory. To avoid massive performance drops from constant spinlocks (global memory locks), responsibilities are strictly divided:
* **Core 0 (Master of Time):** Manages global system time, increments tick counters, and checks blocked lists to wake up tasks whose timeouts have expired.
* **Core 1 (Worker):** Ignores global time management. Its tick interrupt only checks if the currently running task has exhausted its allocated time-slice. If so, it preempts the task and loads the next one.

### The FreeRTOS Tick
The tick is the operating system's heartbeat, triggered by a hardware timer (default: 1000 times per second or 1ms intervals). 
* **Mechanism:** Every millisecond, the hardware timer fires, the CPU pauses the current task, the tick count increments, sleeping tasks are evaluated, and the scheduler may switch tasks.
* **Trade-off:** A higher tick rate provides finer timing precision but consumes more CPU cycles and power.

### Floating-Point Unit (FPU) in ISR
* **Float:** Supported by hardware FPU. If used inside an Interrupt Service Routine (ISR), it requires specific configuration (`CONFIG_FREERTOS_FPU_IN_ISR`).
* **Double:** The ESP32 FPU *does not* support hardware acceleration for double-precision floating-point arithmetic. `double` is implemented via software, making it significantly slower.

---

## 2. Task Management & Separation of Concerns

### ESP-IDF Startup & `app_main`
Unlike vanilla FreeRTOS, you do not call `vTaskStartScheduler()`. ESP-IDF starts FreeRTOS automatically and invokes `app_main()` as the entry point. During startup, background tasks are created automatically:
* **Idle Task:** Priority 0 background task.
* **Timer Task:** Daemon for FreeRTOS Timer API.
* **Main Task:** Executes `app_main()` and deletes itself when the function returns.
* **IPC Tasks:** Handles Inter-Processor Calls.
* **ESPTimer Task:** Executes `esp_timer` callbacks.

*Note: Preemptive priority scheduling means `app_main` has no special privileges once running; it competes for CPU time like any other task.*

### Separation of Execution Threads
To prevent a failing subsystem (like a network timeout) from halting the entire device, decouple your logic into isolated FreeRTOS tasks.
* **Concept:** A `Sensor_Task` reads data while a `Network_Task` handles Wi-Fi. If the network task gets stuck in a retry loop, the FreeRTOS scheduler continues to allocate CPU time to the sensor task.
* **Key APIs:** `xTaskCreate` (spawn tasks), `vTaskDelay` (non-blocking pauses).
* **Rule:** Task functions should typically be infinite loops and must never use the `return` statement.

---

## 3. Memory Management & Task Lifecycle

### Task Deletion and the Idle Task
Calling `vTaskDelete(NULL)` terminates a task immediately. The task loses its "Ready" status and CPU privileges. However, the memory allocated for its Task Control Block (TCB) and Stack is **not freed immediately**. 
* **The Garbage Collector:** The task cannot free its own stack while executing on it. Instead, it marks itself as dead. The **Idle Task** (Priority 0) is responsible for sweeping memory and returning it to the heap.
* **Starvation Warning:** If the CPU is heavily loaded by high-priority tasks, the Idle Task never runs. The dead task's RAM remains blocked ("zombie task"), potentially causing an Out-Of-Memory (OOM) crash.

### Graceful Shutdown Pattern
Never force-kill tasks blindly. Use this sequence:
1.  **Signal:** Task A sets a flag (e.g., via Event Group or thread-safe variable).
2.  **Poll:** Task B checks this flag at a safe point during its `while(1)` loop.
3.  **Cleanup:** Task B detects the signal, breaks the loop, releases mutexes, closes network sockets, and calls `free()` on dynamic memory.
4.  **Terminate:** Task B calls `vTaskDelete(NULL)` as its absolute final instruction.

---

## 4. Inter-Task Communication & Synchronization

### The ESP Event Loop (Event-Driven State)
Wi-Fi and system events are asynchronous. Do not poll for status continuously. 
* **Concept:** Use a Publish/Subscribe pattern. Register a handler to listen for specific events (e.g., `WIFI_EVENT_STA_DISCONNECTED`) and execute background routines like reconnecting.
* **Key APIs:** `esp_event_loop_create_default()`, `esp_event_handler_register()`.

### FreeRTOS Event Groups (Global State Flags)
Use Event Groups to securely share binary states between independent modules.
* **Concept:** A highly optimized collection of bits. The Wi-Fi handler sets `BIT0` to 1 upon connection. A UDP task checks `BIT0` before sending data. If 0, it drops or queues the data without crashing.
* **Key APIs:** `xEventGroupCreate()`, `xEventGroupSetBits()`, `xEventGroupWaitBits()`.

### Data Buffers and Streaming
Different memory scenarios require different buffer types:

| Buffer Type | Best Used For | Memory Efficiency | CPU Overhead |
| :--- | :--- | :--- | :--- |
| **Ping-Pong + DMA** | High-speed ADCs, I2S Audio. Hardware-driven. | Excellent (Zero Copy) | None |
| **ESP Ring Buffer** | Variable length packets. Supports Zero-Copy retrieval via direct pointers. | Excellent (Zero Copy) | Low |
| **Vanilla Stream Buffer** | Simple byte streams (1 sensor to 1 task). | Good (Copy required) | Very Low |
| **FreeRTOS Queue** | Tiny structs, state machine commands. | Poor (Double Copy) | Medium |

#### Ring Buffer Types:
1.  **No-Split (`RINGBUF_TYPE_NOSPLIT`):** Items are stored continuously. Best for structured data packets. May leave empty space at the end of the buffer.
2.  **Allow-Split (`RINGBUF_TYPE_ALLOWSPLIT`):** Maximizes RAM efficiency by splitting an item across the end/beginning boundary. The reading task must handle split processing.
3.  **Byte Buffer (`RINGBUF_TYPE_BYTEBUF`):** Unstructured, continuous stream of bytes (e.g., raw UART, audio streams).

### Queue Sets
If a task needs to listen to multiple sources (e.g., a data ring buffer and a command queue) simultaneously without polling:
* Bind the sources into a **Queue Set**.
* The task blocks on `xQueueSelectFromSet()`.
* When any source receives data, the OS wakes the task. The task then queries which specific source triggered the wake-up and processes it accordingly.

---

## 5. Advanced Execution Contexts

### Hooks
* **Idle Hook (`vApplicationIdleHook`):** Runs in an infinite loop when the OS has no other tasks to run. **Warning:** If you execute blocking code here, the Idle Task cannot perform garbage collection, leading to memory leaks.
* **Tick Hook (`vApplicationTickHook`):** Fires with absolute precision on every hardware tick, before the OS evaluates task scheduling.

### Critical Sections & Suspended Scheduler
* **`vTaskSuspendAll()` / `xTaskResumeAll()`:** Halts the scheduler. Code executed here must be strictly non-blocking and restricted to RAM/CPU register operations. Touching external hardware will cause a crash.
* **Spinlocks:** Used in SMP to protect shared variables. They must be extremely fast (microseconds). **Never** use `printf`, `ESP_LOG`, `vTaskDelay`, network calls, or slow peripherals (I2C/SPI) inside a critical section.

---

## 6. Best Practices & Error Handling

### ESP-IDF Error Decoding
Always translate raw error codes into human-readable strings for easier debugging:

```c
#include "esp_err.h"

esp_err_t err = nvs_get_u32(my_handle, "restart_count", &restart_counter);

if (err != ESP_OK) {
    // esp_err_to_name translates the error code
    ESP_LOGE("NVS", "Error occurred: %s", esp_err_to_name(err));
}
