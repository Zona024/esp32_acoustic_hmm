### **Todo-List: ESP32 Edge Acoustic Classifier**

**System Core & FreeRTOS**

* [x] Implement basic main app skeleton
* [x] Set up Custom Idle Task hooks for CPU profiling (`vApplicationIdleHook`)
* [x] Implement Dummy load task to validate Watchdog Timer (TWDT) and Time-Slicing
* [x] Fix Integer Division logic for Task Delays (Minimum 10ms for 100Hz Tick Rate)
* [x] Enforce Mutex locks for Thread-Safe Terminal output

**Storage & Setup (NVS & WLAN)**

* [x] Initialize NVS Flash memory
* [x] Implement Reboot Counter utilizing NVS Name-Spaces
* [x] Build interactive WLAN Setup prompt via UART
* [x] Refactor `terminal_input_task` to handle backspace and block control chars
* [ ] Implement NVS read logic to auto-load previously saved WLAN credentials on boot
* [ ] Add a timeout to the WLAN interactive prompt (e.g., skip after 10s if no input)
* [ ] Connect LwIP stack using the loaded credentials to actually establish the Wi-Fi connection

**Data Pipeline (Memory & Ringbuffer)**

* [x] Design architecture (Byte Stream vs. Split Stream evaluation)
* [x] Implement static Ringbuffer (`xRingbufferCreateStatic`) to prevent Heap fragmentation
* [ ] Fill ringbuffer with dummy byte data (e.g., an alternating bit pattern)
* [ ] Verify Read/Write Pointers and `xRingbufferReceive` functionality
* [ ] Implement alignment checks (ensure extraction of exactly 16-bit or 32-bit blocks)

**Hardware Integration (I2S Microphone)**

* [ ] Wire the I2S digital microphone (SCK, WS, SD) to the ESP32 GPIOs
* [ ] Configure I2S peripheral (Sample rate, Bit depth, Channel format)
* [ ] Read raw sensor data continuously from I2S driver
* [ ] Print a small batch of raw I2S data to the terminal for a sanity check (Noise vs. Silence)
* [ ] Route the I2S DMA callbacks to push data directly into the static Ringbuffer

**Feature Extraction (DSP/FFT)**

* [ ] Determine the optimal audio frame size for the classifier (e.g., 512 or 1024 samples)
* [ ] Integrate ESP-DSP library or a lightweight custom FFT implementation
* [ ] Fetch complete frames from the Ringbuffer and calculate the Fast Fourier Transform
* [ ] Extract the relevant frequency bands (bins) required as HMM features

**HMM Development (Offline / Python)**

* [ ] Dump raw audio data over UART/Wi-Fi to the PC to build a training dataset
* [ ] Train the Hidden Markov Model locally in Python using `hmmlearn` or similar
* [ ] Export the trained matrices (Transition, Emission, Initial States) into C-Header format (`.h`)

**Edge Classification (C-Implementation)**

* [ ] Implement the Viterbi algorithm in C on the ESP32
* [ ] Feed the extracted FFT features into the Viterbi function
* [ ] Add thresholding/confidence logic (when is a state shift valid?)
* [ ] Print real-time classification state changes to the terminal

**Telemetry & Network**

* [ ] Integrate the `esp_mqtt` client
* [ ] Establish connection to a local MQTT broker
* [ ] Construct JSON payload for state changes
* [ ] Publish the recognized state strictly on transition (event-driven, not continuously)
