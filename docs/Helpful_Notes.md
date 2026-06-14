# ESP32 and FreeRTOS: Comprehensive Architecture & Development Guide

This document outlines core concepts, architectural principles, and best practices for developing robust applications using ESP-IDF and FreeRTOS on the ESP32.

## Table of Contents
1. [Architecture & OS Fundamentals](#1-architecture--os-fundamentals)
2. [Task Management & Separation of Concerns](#2-task-management--separation-of-concerns)
3. [Memory Management & Task Lifecycle](#3-memory-management--task-lifecycle)
4. [Inter-Task Communication & Synchronization](#4-inter-task-communication--synchronization)
5. [Advanced Execution Contexts (Hooks & Critical Sections)](#5-advanced-execution-contexts)
6. [Best Practices & Error Handling](#6-best-practices--error-handling)

7. [TLSP Deletion Callback (ESP-IDF)](#tlsp-deletion-callback-esp-idf)

8. [Advanced Heap Memory Allocation & Capabilities](#advanced-heap-memory-allocation--capabilities)
9. [Heap Debugging & Monitoring](#heap-debugging--monitoring)

10. [Memory Management Unit (MMU) Mapping](#memory-management-unit-mmu-mapping)

11. [ESP32-WROOM-32 — Key Specifications](#11-esp32-wroom-32--key-specifications-esp32-specs)

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
```

---

## 7. TLSP Deletion Callback (ESP-IDF) 

A **TLSP Deletion Callback** is a function that ESP-IDF automatically calls when a task is deleted.

### Why Use It?

Use it to automatically free resources that belong exclusively to a task, especially dynamically allocated memory.

This prevents memory leaks if the task is terminated unexpectedly or from another task.

### How It Works

1. Allocate memory for the task.
2. Store the pointer in a Thread Local Storage Pointer (TLSP).
3. Register a deletion callback.
4. When the task is deleted, ESP-IDF automatically executes the callback.
5. The callback cleans up the memory (e.g. `free(ptr)`).

### When to Use

- Memory lifetime = task lifetime.
- Resources are owned only by that task.
- Automatic cleanup is desired.

### When Not to Use

- Another task still needs the data after the task exits.
- Large temporary buffers should be freed earlier to save RAM.

### Rule

> If a resource should exist only while a task exists, attach it to the task using a TLSP Deletion Callback.

### Example

```c
#define TLS_INDEX 0

static void cleanup_callback(int index, void *ptr)
{
    free(ptr);
}

static void my_task(void *arg)
{
    int *buffer = malloc(1024);

    vTaskSetThreadLocalStoragePointerAndDelCallback(
        NULL,                 // current task
        TLS_INDEX,
        buffer,
        cleanup_callback
    );

    // Use buffer...

    vTaskDelete(NULL);
}
```

---

## 8. Advanced Heap Memory Allocation & Capabilities 

The ESP32 has multiple memory regions (Internal RAM, PSRAM, IRAM, RTC RAM) with different properties. When memory must meet specific hardware requirements, use the **Capabilities-Based Allocator**.

### Standard `malloc()`

```c
void *ptr = malloc(1024);
```

- Allocates normal 8-bit accessible memory.
- Usually comes from internal RAM.
- Can use PSRAM automatically if configured in `menuconfig`.

### `heap_caps_malloc()`

Use when memory requires specific capabilities:

```c
void *ptr = heap_caps_malloc(size, capabilities);
```

#### Common Capabilities

| Capability | Purpose |
|------------|---------|
| `MALLOC_CAP_DMA` | DMA-accessible memory for SPI, I2S, etc. |
| `MALLOC_CAP_SPIRAM` | Allocate in external PSRAM |
| `MALLOC_CAP_EXEC` | Executable memory (IRAM) |
| `MALLOC_CAP_32BIT` | 32-bit access only |
| `MALLOC_CAP_RTCRAM` | Retained during deep sleep |

#### Example

```c
uint8_t *dma_buffer =
    heap_caps_malloc(1024, MALLOC_CAP_DMA);

if (dma_buffer == NULL) {
    ESP_LOGE("MEM", "Allocation failed");
}

free(dma_buffer);
```

> Memory allocated with `heap_caps_malloc()` is freed using the normal `free()` function.

---

## 9. Heap Debugging & Monitoring 

ESP-IDF provides tools for monitoring memory usage, fragmentation, and corruption.

### Querying Heap State

Check memory availability before large allocations:

```c
size_t free_mem =
    heap_caps_get_free_size(MALLOC_CAP_8BIT);

size_t largest_block =
    heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM);
```

#### Useful Metrics

- **Free Size** → Total available memory.
- **Largest Free Block** → Helps identify heap fragmentation.

### Heap Corruption Detection (Poisoning)

Heap Poisoning detects buffer overflows and memory corruption.

How it works:

1. ESP-IDF places guard bytes around allocations.
2. `free()` verifies the guard bytes.
3. If corruption is detected, the system immediately reports an error.

Recommended during development.

### Out of Memory (OOM) Handling

By default:

```c
malloc(...) == NULL
```

when allocation fails.

For development, enable:

```text
Panic on Failed Allocation
```

in `menuconfig`.

This causes an immediate crash with a stack trace, making allocation failures easier to debug.

### Rule

> Use `malloc()` for normal allocations and `heap_caps_malloc()` when memory requires specific capabilities such as DMA, PSRAM, IRAM, or RTC RAM.



## 10. Memory Management Unit (MMU) Mapping 

The MMU allows physical Flash or PSRAM memory to be mapped directly into the CPU's virtual address space.

Instead of reading data into a RAM buffer with `spi_flash_read()`, the CPU can access external memory through a normal pointer.

### Why Use It?

- **Zero-Copy Access** → No temporary RAM buffer required.
- **RAM Conservation** → Large assets stay in Flash/PSRAM.
- **Simple Access** → Data is accessed like a normal array.
- **Execution In Place (XIP)** → Code can execute directly from Flash.

### How It Works

```text
Physical Flash/PSRAM
          ↓
         MMU
          ↓
 Virtual Address
          ↓
         CPU
```

After mapping:

```c
const uint8_t *data = mapped_ptr;

uint8_t value = data[0];
```

The CPU accesses the data directly through the mapped virtual address.

### Performance Notes

MMU mapping does **not** eliminate CPU usage.

The CPU still accesses the data, but:

- No explicit `spi_flash_read()` calls are needed.
- No RAM copy is required.
- Flash data is automatically fetched through the cache.

Performance hierarchy:

```text
Internal SRAM   (Fastest)
      ↓
Cache Hit
      ↓
PSRAM
      ↓
Flash Cache Miss (Slowest)
```

MMU mapping is primarily a **RAM optimization**, not a speed optimization.

### MMU Page Alignment

MMU mappings must follow hardware page boundaries.

Requirements:

- Physical address must be page-aligned.
- Mapping size must be page-aligned.
- Virtual address is page-aligned.

The page size depends on the ESP32 target.

### Example

```c
const void *mapped_ptr = NULL;

esp_err_t err = esp_mmu_map(
    0x100000,
    1024 * 1024,
    MMU_TARGET_FLASH0,
    MMU_MEM_CAP_READ,
    0,
    &mapped_ptr
);

if (err == ESP_OK) {
    uint8_t first_byte =
        ((const uint8_t *)mapped_ptr)[0];

    esp_mmu_unmap(mapped_ptr);
}
```

---

### VFS (Virtual File System)

The VFS provides a standard file API that works across different filesystems such as:

- SPIFFS
- LittleFS
- FATFS (SD cards)

Instead of directly accessing memory, you work with files.

Example:

```c
FILE *f = fopen("/spiffs/config.txt", "r");

fread(buffer, 1, sizeof(buffer), f);

fclose(f);
```

VFS handles:

- File names
- Directories
- Read/Write operations
- Filesystem management
- Wear leveling

### VFS vs MMU Mapping

| VFS | MMU Mapping |
|------|------------|
| File-based access | Direct memory access |
| Uses `fopen()`, `fread()`, `fwrite()` | Uses pointers |
| Supports files and directories | No file abstraction |
| Can read and write files | Typically read/execute access |
| More overhead | Minimal overhead |
| Requires RAM buffers | Zero-copy access |

### Use Cases

#### Use VFS When

You need a filesystem:

```text
/config.json
/settings.txt
/logs/log.txt
/data/user.dat
```

Typical operations:

```c
fopen()
fread()
fwrite()
```

Examples:

- Configuration files
- Log files
- User data
- SD card storage
- General file management

---

#### Use MMU Mapping When

You have large static binary data:

```text
Fonts
Images
Audio Samples
Machine Learning Models
Lookup Tables
```

Typical access:

```c
const uint8_t *data = mapped_ptr;
```

Examples:

- Audio playback assets
- Embedded font data
- Large image resources
- Read-only databases
- Executing code directly from Flash (XIP)

### Rule

> Use **VFS** when you need files and filesystem functionality.
>
> Use **MMU Mapping** when you need direct, zero-copy access to large Flash/PSRAM data.


## 11. ESP32-WROOM-32 — Key Specifications {#esp32-specs}

### 1. Core Architecture

- **Microprocessor:** Xtensa Dual-Core 32-bit LX6  
- **Clock Speed:** up to 240 MHz  
- **Crystal Oscillator:** 40 MHz integrated

---

### 2. Memory & Storage

- **SRAM:** 520 KB internal RAM  
- **ROM:** 448 KB internal ROM  
- **RTC SRAM:** 8 KB (retained in Deep Sleep)  
- **Flash:** 4 MB external SPI Flash (module-integrated)

---

### 3. Wireless Connectivity

- **Wi-Fi:** 802.11 b/g/n (up to ~150 Mbps theoretical)  
- **Bluetooth:** Classic Bluetooth v4.2 + BLE  
- **Antenna:** On-board PCB antenna

---

### 4. Peripherals & Interfaces

- **GPIO:** up to 32 usable pins  
  - 5 are **strapping pins** (affect boot configuration)

- **Digital Interfaces:**
  - UART  
  - SPI  
  - I2C  
  - I2S  
  - SDIO  
  - TWAI® (CAN-compatible)

- **Analog:**
  - 2× 12-bit SAR ADC (up to 18 channels)
  - 2× 8-bit DAC

- **Hardware Features:**
  - LED PWM controller  
  - Motor PWM controller  
  - Pulse counters  
  - Capacitive touch (10 GPIOs)

---

### 5. Power & Operating Conditions

- **Supply Voltage:** 3.0 V – 3.6 V (typical 3.3 V)  
- **Peak Current Requirement:** ≥ 500 mA (important for Wi-Fi bursts)  
- **Operating Temperature:** –40 °C to 85 °C  

---
