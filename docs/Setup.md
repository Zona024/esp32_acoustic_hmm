# ESP32 Acoustic HMM - Setup Guide

### Step 1: Install System Dependencies

```fish
paru -S --needed gcc git make flex bison gperf python cmake ninja ccache dfu-util libusb

```

### Step 2: Grant USB Port Permissions

```fish
sudo usermod -aG uucp $USER

```

_Note: You must completely log out and log back in (or reboot) for these group changes to take effect._

### Step 3: Install the ESP-IDF Toolchain

```fish
mkdir -p ~/esp
cd ~/esp
git clone -b v5.2.1 --recursive [https://github.com/espressif/esp-idf.git](https://github.com/espressif/esp-idf.git)
cd esp-idf
./install.sh esp32

```

### Step 4: Clone the Project Repository

```fish
cd ~
git clone [https://github.com/Zona024/esp32_acoustic_hmm.git](https://github.com/Zona024/esp32_acoustic_hmm.git)
cd esp32_acoustic_hmm

```

### Step 5: Load Environment & Set Target

```fish
source ~/esp/esp-idf/export.fish
idf.py set-target esp32

```

### Step 6: Configure FreeRTOS Kernel

To ensure the custom `vApplicationIdleHook` function works and the idle counters start correctly, the FreeRTOS kernel hooks must be enabled in the project configuration.

Run the menu configuration tool:

```fish
idf.py menuconfig

```

Navigate through the menus using arrow keys and press `Space` to enable the following settings:

1. Go to: **`Component config`** -> **`FreeRTOS`** -> **`Kernel`**
2. Enable: **`Enable FreeRTOS Idle Hook`**

```fish
idf.py reconfigure

```

```

```
