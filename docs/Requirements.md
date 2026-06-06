# Project Definition: Acoustic Environment Monitoring via Edge Computing and Hidden Markov Models

## Project Goal and Functionality

The project involves the development of an intelligent, network-capable sensor node based on an ESP32 microcontroller. The goal is the local, hardware-level classification of complex ambient sounds (e.g., birdsong, rain, or street noise) and the transmission of the determined environmental status via the MQTT protocol.

Instead of streaming bandwidth-intensive raw audio data over the network, the complete signal processing occurs directly on the microcontroller (Edge Computing):

* **Data Acquisition:** The digital audio signal is read non-blocking into the ESP32's RAM via an I²S interface using Direct Memory Access (DMA).
* **Feature Extraction:** The time-series audio data is converted into the frequency domain using a Fast Fourier Transform (FFT) to isolate spectral features (such as MFCCs).
* **Classification (HMM):** A Hidden Markov Model (HMM) is used for pattern recognition and analysis of the temporal sequence of sounds. The Viterbi algorithm is implemented in C and compares the real-time frequency data with probability matrices (transition and emission matrices) that were previously trained on a PC and exported into the C code.
* **Data Transmission:** As soon as the algorithm classifies a sound pattern with high probability, the ESP32 sends a compact text message (e.g., `{"environment": "Nature"}`) to a central MQTT broker.

## Bill of Materials: Required Hardware and Components

The following physical components are required to build the prototype:

* **Microcontroller:** ESP32 Development Board (e.g., ESP32-WROOM-32, NodeMCU ESP32). Provides the necessary processing power for the FFT, sufficient RAM for audio buffers, and integrated Wi-Fi connectivity.
* **Sensors:** Digital MEMS microphone with an I²S interface.
    * *Recommended Modules:* INMP441 or SPH0645. Analog microphones or I²C microphones are not suitable for continuous, low-noise audio streaming and cannot provide the necessary data rate for this project.
* **Prototyping Material:**
    * A standard breadboard.
    * Jumper wires (female-to-male or male-to-male, depending on the pins of the ESP32 board and the microphone).
* **Power Supply:**
    * A micro-USB or USB-C cable (depending on the ESP32 board) for power supply and flashing the firmware.

## Required Software Infrastructure

* **Embedded Toolchain:** ESP-IDF (Espressif IoT Development Framework) for hardware-level programming in C, task management (FreeRTOS), and MQTT integration.
* **Offline Training Environment:** A PC with a Python environment (libraries such as `numpy`, `scipy`, and `hmmlearn`) to process the training audio data and generate the static C arrays for the HMM.
* **Network:** A Wi-Fi access point and an accessible MQTT broker (e.g., Eclipse Mosquitto, installed locally on a computer or server within the same network).
