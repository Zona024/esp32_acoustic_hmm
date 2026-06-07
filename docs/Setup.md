### Step 1: Install System Dependencies

The new machine needs the basic C compilation tools and Python.

**For Arch Linux:**

```bash
sudo pacman -S --needed gcc git make flex bison gperf python cmake ninja ccache dfu-util libusb
```

**For Ubuntu / Debian:**

```bash
sudo apt install git wget flex bison gperf python3 python3-pip python3-venv cmake ninja-build ccache libffi-dev libssl-dev dfu-util libusb-1.0-0
```

### Step 2: Grant USB Port Permissions

To flash the microcontroller, your user must be part of the serial interface group.

**For Arch Linux:**

```bash
sudo usermod -aG uucp $USER
```

**For Ubuntu / Debian:**

```bash
sudo usermod -aG dialout $USER
```

_Note: You must completely log out and log back in (or reboot) for these group changes to take effect._

### Step 3: Install the ESP-IDF Toolchain

Download the specific framework version used for the project and install the Xtensa cross-compiler.

```bash
mkdir -p ~/esp
cd ~/esp
git clone -b v5.2.1 --recursive https://github.com/espressif/esp-idf.git
cd esp-idf
./install.sh esp32
```

### Step 4: Clone the Project Repository

```bash
cd ~
git clone https://github.com/Zona024/esp32_acoustic_hmm.git
cd esp32-acoustic-hmm
```

### Step 5: Load Environment & Prepare Editor

Activate the toolchain variables for your current terminal session and generate the build files.

**Load the environment:**

```bash
# If using Fish shell:
source ~/esp/esp-idf/export.fish

# If using Bash or Zsh:
. ~/esp/esp-idf/export.sh
```

**Set the target and generate LSP files:**

```bash
idf.py set-target esp32
idf.py reconfigure
```
