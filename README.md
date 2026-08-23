# cougarLCD.cpp

An unofficial C++ based controller for the 9.16-inch LCD used in the COUGAR CFV235
case family. It provides a lightweight alternative to COUGAR LCD Editor for
displaying a clock and live CPU/GPU information.

This project only controls the LCD. Case lighting and fans remain under your
normal RGB or motherboard software.

## Supported hardware

The project supports the LCD included with:

- [COUGAR CFV235 Vision](https://cougargaming.com/us/products/cases/cfv235-vision/)
- [COUGAR CFV235 Mesh Vision](https://cougargaming.com/us/products/cases/cfv235-mesh-vision/)
- [COUGAR CFV235 LCD Monitor](https://cougargaming.com/products/cases/cfv235-lcd-monitor/)

The panel is 9.16 inches with a resolution of 1920 × 462. Its USB identity is
`VID_1D6B/PID_0126` and it uses 1025-byte HID input and output reports.

## Features

- Live clock and date
- CPU temperature and load
- NVIDIA GPU temperature and load
- Configurable brightness and refresh interval
- Upload support for your own PNG image
- Automatic startup through WSL and systemd
- Native Windows read-only HID diagnostic tool
- SignalRGB lighting coexistence on systems affected by sensor-driver conflicts

The default dashboard is drawn in C++ with Cairo and system fonts. No COUGAR
backgrounds, fonts, firmware, or other media assets are included.

## Requirements

For the live dashboard:

- Windows 11
- [WSL2](https://learn.microsoft.com/windows/wsl/install) with Ubuntu
- [usbipd-win](https://github.com/dorssel/usbipd-win)
- The LCD connected by USB

The installer adds the required Ubuntu development packages: CMake, Ninja,
Cairo, Fontconfig, hidapi, and DejaVu fonts.

The Windows diagnostic tool is optional. Building it requires Visual Studio or
Build Tools for Visual Studio with the **Desktop development with C++**
workload. The free Build Tools edition is sufficient.

## Install

Open PowerShell as Administrator:

```powershell
wsl --install -d Ubuntu
winget install --interactive --exact --id dorssel.usbipd-win
git clone git@github.com:roychowdhuryrohit-dev/cougarLCD.cpp.git
cd cougarLCD.cpp
Set-ExecutionPolicy -Scope Process Bypass
.\scripts\windows\Install.ps1 -Distro Ubuntu
```

If your Ubuntu installation has a versioned name, use that exact name. For
example:

```powershell
.\scripts\windows\Install.ps1 -Distro Ubuntu-26.04
```

The installer builds the WSL client, installs its systemd service, shares the
LCD with WSL through usbipd-win, and creates a hidden logon task that reconnects
the display after a restart. It does not create Desktop shortcuts or visible
startup terminals.

Check the service with:

```powershell
wsl -d Ubuntu -- systemctl status cougar-lcd --no-pager
wsl -d Ubuntu -- journalctl -u cougar-lcd -n 50 --no-pager
```

## Build manually

### WSL client

```bash
sudo apt update
sudo apt install build-essential cmake ninja-build pkg-config \
  libcairo2-dev libfontconfig1-dev libhidapi-dev fonts-dejavu-core

cmake -S . -B build/wsl -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build/wsl
ctest --test-dir build/wsl --output-on-failure
```

Render a local test image without accessing USB:

```bash
./build/wsl/src/linux/cougar-lcd --render-only /tmp/dashboard.png
```

List compatible LCD interfaces without sending any reports:

```bash
./build/wsl/src/linux/cougar-lcd --probe
```

### Windows diagnostic tool

```powershell
.\scripts\windows\Build.ps1
.\build\windows-x64\tools\windows\Release\cougar-hid-probe.exe
```

The Windows tool is read-only. It can also capture incoming reports for a
limited time:

```powershell
.\build\windows-x64\tools\windows\Release\cougar-hid-probe.exe --listen-ms 10000
```

## Usage

The installed service starts the live dashboard automatically. Common manual
commands are:

```bash
cougar-lcd --probe
cougar-lcd --render-only /tmp/frame.png
cougar-lcd --upload /path/to/your-image.png
cougar-lcd --brightness 65 --interval-ms 1500
```

Persistent service options are stored in `/etc/default/cougar-lcd`:

```bash
COUGAR_LCD_ARGS="--quiet --brightness 65 --interval-ms 1500"
sudo systemctl restart cougar-lcd
```

Your own font can be selected with `--font /path/to/font.ttf`.

## Temperature monitoring

NVIDIA GPU values are read through the NVML library supplied by the Windows
NVIDIA driver for WSL.

Windows does not provide a general CPU-temperature API. On supported AMD Ryzen
systems, install the official AMD Ryzen Master Monitoring SDK and then run:

```powershell
.\scripts\windows\Install-AmdTelemetry.ps1
```

The AMD logger writes its readings to `C:\ProgramData\CougarLCD`. The WSL
service reads the newest active CSV file. The AMD SDK itself is not included in
this repository.

Without AMD telemetry, CPU temperature displays `-- °C` and CPU load is based
on WSL activity. GPU values display `-- °C` when NVML is unavailable.

## Using SignalRGB for lighting

Some systems cannot start AMD telemetry after SignalRGB's hardware-monitoring
driver has loaded. SignalRGB lighting can still be used by disabling its
hardware monitoring and starting the programs in the correct order:

1. Start AMD telemetry.
2. Wait for a valid temperature sample.
3. Start SignalRGB with hardware monitoring disabled.

The included installer configures this automatically:

```powershell
.\scripts\windows\Install-SignalRgbCoexistence.ps1
```

It creates a hidden logon task named `COUGAR LCD Sensor Startup Order`. The task
stops any stale HWiNFO sensor driver, starts the AMD logger, waits for a valid
CSV sample, starts SignalRGB, and checks that AMD readings continue afterward.
SignalRGB lighting remains available.

The script backs up the settings it changes. It does not delete or modify
SignalRGB or HWiNFO files. To restore the previous startup and monitoring
settings:

```powershell
.\scripts\windows\Uninstall-SignalRgbCoexistence.ps1
```

The ordering log is stored at:

```text
C:\ProgramData\CougarLCD\sensor-startup-order.log
```

## Uninstall

Run the installers you used in reverse order from PowerShell as Administrator:

```powershell
.\scripts\windows\Uninstall-SignalRgbCoexistence.ps1
.\scripts\windows\Uninstall-AmdTelemetry.ps1
.\scripts\windows\Uninstall.ps1
```

The main uninstaller disables the service and removes its startup task. It
keeps the installed binary, USB binding, AMD SDK, and CSV data unless you pass
`-RemoveConfiguration`.

## FAQ

### Can COUGAR LCD Editor run at the same time?

No. Close COUGAR LCD Editor before attaching the LCD to WSL. Only one program
should control the HID interface at a time.

### Why is CPU temperature blank?

Install and start the AMD telemetry bridge on a supported Ryzen system. Check
that the newest `RMSDK_Parameter_log_cougar_*.csv` file in
`C:\ProgramData\CougarLCD` is still updating.

### Why does CPU load differ from Task Manager?

Without AMD telemetry, the program reads WSL's `/proc/stat`, which measures WSL
activity rather than total Windows activity. With AMD telemetry running, it
uses the SDK's core residency readings.

### How does the SignalRGB workaround work?

The AMD monitoring SDK and SignalRGB's HWiNFO-based monitoring can compete for
sensor access. The scheduled task gives AMD the first opportunity to initialize
and produce a valid reading. SignalRGB starts afterward with its monitoring
features disabled, while its lighting controls continue to run.

### Why is the display blank after restarting Windows?

Check `usbipd list` and the `cougar-lcd` systemd status. The most common causes
are a USB device that was not reattached, a changed WSL distribution name, or
COUGAR LCD Editor still running.

### Can I display my own image?

Yes. Use a PNG sized for the 1920 × 462 panel:

```bash
cougar-lcd --upload /path/to/your-image.png
```

## More information

- [Protocol notes](docs/PROTOCOL.md)
- [Troubleshooting](docs/TROUBLESHOOTING.md)
- [Contributing](CONTRIBUTING.md)
- [Security policy](SECURITY.md)

## License

This project is available under the [MIT License](LICENSE). COUGAR, AMD,
NVIDIA, SignalRGB, and HWiNFO are trademarks of their respective owners.
