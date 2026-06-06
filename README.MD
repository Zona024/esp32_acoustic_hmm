# ESP32 Edge Acoustic Classifier

## Project Overview
This project is an edge-computing initiative aimed at classifying ambient acoustic environments (such as nature, rain, or urban noise) directly on an ESP32 microcontroller. 

Instead of streaming high-bandwidth raw audio over a network, this system captures sound, processes the frequency features locally, and uses a Hidden Markov Model (HMM) to identify the environment. Only the final, lightweight classification state is published via MQTT. This approach drastically reduces network traffic, optimizes power consumption, and ensures privacy.

> **Status:** This project is currently in the initial conceptual and planning phase. Hardware selection and core architecture are being established.

## Planned System Architecture
The system is designed around a decoupled data pipeline, prioritizing hardware-close execution and efficient memory management:

1. **Audio Acquisition:** Capturing continuous ambient sound using a digital microphone via an I²S interface.
2. **Feature Extraction:** Converting the raw time-domain audio data into the frequency domain (e.g., using Fast Fourier Transform) to isolate distinct acoustic patterns.
3. **Edge Classification:** Running a custom C-implementation of a Hidden Markov Model (HMM) directly on the microcontroller to analyze the sequence of the audio features.
4. **Telemetry:** Publishing the recognized environmental state asynchronously to a central MQTT broker.

## Technology Stack (Conceptual)
* **Hardware:** ESP32 Development Board, Digital I²S MEMS Microphone.
* **Firmware:** Standard C utilizing the ESP-IDF (Espressif IoT Development Framework) and FreeRTOS for task management.
* **Model Training:** Python-based offline environment for analyzing audio samples and generating the HMM transition/emission matrices.

## Project Roadmap
- [ ] Hardware prototyping and I²S audio capture validation.
- [ ] Implementation of local frequency analysis (FFT).
- [ ] Python-based offline HMM training with sample data.
- [ ] Porting the HMM classifier (Viterbi algorithm) to the ESP32.
- [ ] Integration of the MQTT client and final system testing.
