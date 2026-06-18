# ESP32 Edge Acoustic Classifier

## Project Overview

This project is an edge-computing initiative aimed at classifying ambient acoustic environments (such as nature, rain, or urban noise) directly on an ESP32 microcontroller.

Instead of streaming high-bandwidth raw audio over a network, this system captures sound, processes the frequency features locally, and uses a Hidden Markov Model (HMM) to identify the environment. Only the final, lightweight classification state is published via MQTT. This approach drastically reduces network traffic, optimizes power consumption, and ensures privacy.

> **Status:** Core system architecture, FreeRTOS task scheduling, and memory-safe data pipelines are successfully implemented. The system currently handles non-volatile storage (NVS), interactive Wi-Fi provisioning, and static ring buffers for zero-copy audio stream management. Currently focusing on hardware integration (I²S microphone).

## Potential Use Cases

By processing audio locally and transmitting only abstract state changes, this architecture enables highly scalable and privacy-compliant monitoring applications:

* **Bioacoustics & Ecological Research:** Autonomous monitoring of local avian populations. Edge nodes deployed in natural habitats can identify and tally specific bird calls, allowing researchers to track biodiversity, migration patterns, and population density over time without the bandwidth and storage costs of streaming raw audio to a server.

* **Acoustic Appliance Monitoring (Smart Home/Predictive Maintenance):** State detection for non-smart household appliances. The classifier can be trained on the specific acoustic and vibrational signatures of a washing machine's spin cycle. Once the cycle completion is recognized, the system can trigger a lightweight webhook to send an automated notification (e.g., a WhatsApp message or a Home Assistant alert) while maintaining strict acoustic privacy within the home.

## System Architecture & Core Features

The system is designed around a decoupled, highly efficient data pipeline, prioritizing hardware-close execution and deterministic memory management:

* **RTOS Orchestration:** Multi-core task scheduling using FreeRTOS. Features custom idle-task monitoring to prevent CPU starvation and safely manage the Task Watchdog Timer (TWDT).

* **Deterministic Memory Management:** Zero-leak architecture utilizing statically allocated FreeRTOS Byte Buffers (`xRingbufferCreateStatic`) for handling continuous, high-bandwidth audio streams without heap fragmentation.

* **Persistent Storage & Connectivity:** Robust NVS management for state tracking across reboots and an interactive, mutex-protected UART terminal interface for runtime Wi-Fi provisioning.

* **Audio Acquisition (In Progress):** Capturing continuous ambient sound using a digital microphone via an I²S interface directly into the static byte buffer.

* **Edge Classification (Planned):** Running a custom C-implementation of a Hidden Markov Model (HMM) directly on the microcontroller to analyze frequency features (Viterbi algorithm).

## Technology Stack

* **Hardware:** ESP32 Development Board, Digital I²S MEMS Microphone.

* **Firmware:** Standard C utilizing the ESP-IDF (Espressif IoT Development Framework) and FreeRTOS.

* **Model Training:** Python-based offline environment for analyzing audio samples and generating the HMM transition/emission matrices.

## Project Roadmap

* [x] Core RTOS setup, Idle-task monitoring, and CPU load profiling.

* [x] Implementation of NVS storage and interactive UART-based Wi-Fi setup.

* [x] Design of memory-safe (static) Ringbuffer pipeline for audio streams.

* [ ] Hardware integration: I²S microphone setup and DMA audio capture.

* [ ] Implementation of local frequency analysis (FFT).

* [ ] Python-based offline HMM training with sample data.

* [ ] Porting the HMM classifier (Viterbi algorithm) to the ESP32.

* [ ] Integration of the MQTT client and final system testing.
