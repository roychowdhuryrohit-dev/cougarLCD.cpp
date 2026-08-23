# cougarLCD.cpp

An independent, asset-free C++ dashboard and USB transport for the **COUGAR
CFV235-series 9.16-inch LCD monitor**. It replaces the display portion of
COUGAR LCD Editor for the hardware documented below; it does not control case
lighting or fans.

> [!IMPORTANT]
> This is an unofficial community project. It is not affiliated with or
> endorsed by COUGAR. Keep COUGAR LCD Editor closed while this service owns the
> display.

## Supported hardware

The implementation is built for the LCD fitted to **COUGAR CFV235 Vision** and
**CFV235 Mesh Vision**, and for the separately sold **CFV235 LCD Monitor**
add-on used across the CFV235 series.

| Property | Tested value |
|---|---|
| Display | 9.16-inch, 1920 × 462 |
| USB identity | COUGAR Inc. `VID_1D6B` / `PID_0126` |
| HID usage | Vendor page `0xFF00`, usage `0x0001` |
| HID reports | 1025-byte input and output reports |
| Tested device build | app/firmware `V1.0.5`, SDK `V1.2.7`, hardware `V2.0` |
| Tested host | Windows 11 with WSL2 Ubuntu 26.04 |

USB cannot distinguish CFV235 Vision from CFV235 Mesh Vision: both expose the
same LCD module. COUGAR's product pages list the screen as 1920 × 462; some
retail copy rounds it to 1920 × 460.

Do **not** select look-alike ThermalTake interfaces such as
`VID_264A/PID_22C5`. They are different devices and are intentionally excluded.

## What it does

- Renders a live 1920 × 462 dashboard entirely in C++—no bundled images or
  proprietary fonts.
- Shows time, CPU temperature/load, and NVIDIA GPU temperature/load.
- Uploads PNG frames through the confirmed media transport used by the LCD.
- Runs as a quiet WSL systemd service and reconnects after Windows sign-in.
- Includes a native Windows **read-only** HID enumerator/logger.
- Accepts a user-owned PNG with `--upload` without adding that asset to Git.

The live Linux/WSL client only implements commands observed and validated on
the tested display: resume, brightness, and PNG media transport. It does not
flash firmware, erase storage, or send guessed commands.

## Architecture

```text
Windows host metrics (optional AMD CSV)      NVIDIA WSL NVML
                   \                            /
                    WSL C++ renderer (Cairo)
                              |
                      PNG media transport
                              |
                  hidapi -> usbipd-win -> LCD
```

The USB device is attached to WSL with `usbipd-win`, so Windows and WSL cannot
own it simultaneously. SignalRGB can still control lighting because the LCD
USB interface is separate from the case's lighting controller.

## Prerequisites

### Required for the live dashboard

- Windows 11 (Windows 10 with current WSL2 may work but is untested)
- [WSL2](https://learn.microsoft.com/windows/wsl/install) with Ubuntu
- [usbipd-win](https://github.com/dorssel/usbipd-win)
- A connected COUGAR LCD exposing `1d6b:0126`
- Inside WSL: CMake 3.20+, a C++17 compiler, Ninja, pkg-config, Cairo,
  Fontconfig, hidapi-hidraw, and DejaVu fonts

The installer installs the Ubuntu packages automatically.

### Optional Windows diagnostic build

- Visual Studio 2022 or **Build Tools for Visual Studio 2022**
- The `Desktop development with C++` workload and CMake component

A paid Visual Studio license is not required merely to build this open-source
project with the free Build Tools; follow Microsoft's license terms for your
organization and usage.

### Optional metrics

- NVIDIA GPU: the normal NVIDIA Windows driver with WSL GPU support; the client
  loads WSL's NVML library dynamically.
- AMD Ryzen CPU temperature: the official **AMD Ryzen Master Monitoring SDK**.
  The repository does not download or redistribute AMD software.

Without the AMD bridge, CPU load uses `/proc/stat` (WSL activity) and CPU
temperature displays `-- °C`. GPU values display `-- °C` if NVML is unavailable.

## Quick install

Open an elevated PowerShell window:

```powershell
wsl --install -d Ubuntu
winget install --interactive --exact --id dorssel.usbipd-win
git clone https://github.com/roychowdhuryrohit-dev/cougarLCD.cpp.git
cd cougarLCD.cpp
Set-ExecutionPolicy -Scope Process Bypass
.\scripts\windows\Install.ps1 -Distro Ubuntu
```

If your distribution has a versioned name, use it exactly—for example
`-Distro Ubuntu-26.04`. `Install.ps1` does the following:

1. Builds and tests the Linux executable in WSL.
2. Installs `cougar-lcd` and its systemd unit.
3. Binds only USB hardware ID `1d6b:0126` to usbipd-win.
4. Creates one hidden logon task to attach USB and restart the WSL service.
5. Starts the dashboard immediately.

It creates no Desktop shortcuts and no visible startup terminal.

Verify it:

```powershell
wsl -d Ubuntu -- systemctl status cougar-lcd --no-pager
wsl -d Ubuntu -- journalctl -u cougar-lcd -n 50 --no-pager
```

## Build manually

### WSL / Linux client

```bash
sudo apt update
sudo apt install build-essential cmake ninja-build pkg-config \
  libcairo2-dev libfontconfig1-dev libhidapi-dev fonts-dejavu-core
cmake -S . -B build/wsl -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build/wsl
ctest --test-dir build/wsl --output-on-failure
```

Safe tests that do not write to USB:

```bash
./build/wsl/src/linux/cougar-lcd --render-only /tmp/dashboard.png
./build/wsl/src/linux/cougar-lcd --probe
```

The first command performs no USB access. `--probe` enumerates the target but
sends no reports.

### Windows diagnostic probe

From PowerShell:

```powershell
.\scripts\windows\Build.ps1
.\build\windows-x64\tools\windows\Release\cougar-hid-probe.exe
```

To passively capture input for ten seconds:

```powershell
.\build\windows-x64\tools\windows\Release\cougar-hid-probe.exe --listen-ms 10000
```

The Windows tool contains no write path. Close the WSL service and detach the
device first if Windows cannot see it.

## Usage

```text
cougar-lcd --probe
cougar-lcd --render-only /tmp/frame.png
cougar-lcd --upload /path/to/your-own-1920x462.png
cougar-lcd --brightness 65 --interval-ms 1500
```

For persistent options, edit `/etc/default/cougar-lcd` in WSL:

```bash
COUGAR_LCD_ARGS="--quiet --brightness 65 --interval-ms 1500"
sudo systemctl restart cougar-lcd
```

An optional font can be supplied with `--font /path/to/font.ttf`. No font or
media asset is included in this repository.

## AMD CPU temperature

Install the official AMD Ryzen Master Monitoring SDK, then run from elevated
PowerShell:

```powershell
.\scripts\windows\Install-AmdTelemetry.ps1
```

The script validates AMD's Authenticode signature, starts AMD's installed
sample logger without a console window, and writes CSV files under
`C:\ProgramData\CougarLCD`. The WSL client reads only fresh matching CSV files.

Ryzen monitoring drivers can be exclusive. HWiNFO, Fan Control, motherboard
tools, or SignalRGB hardware monitoring may prevent AMD telemetry from opening.
You can keep SignalRGB for lighting, but disable its system/hardware monitoring
features and make sure AMD telemetry starts before SignalRGB. See
[Troubleshooting](docs/TROUBLESHOOTING.md).

## Uninstall

From elevated PowerShell:

```powershell
.\scripts\windows\Uninstall-AmdTelemetry.ps1   # only if installed
.\scripts\windows\Uninstall.ps1
```

The default uninstall preserves the built binary, USB binding, SDK, and metric
CSV files. Pass `-RemoveConfiguration` to remove `C:\ProgramData\CougarLCD` as
well. To remove the Linux binary manually:

```powershell
wsl -d Ubuntu -u root -- rm -f /usr/local/bin/cougar-lcd
```

## FAQ

### Why use WSL instead of a native Windows live client?

The original failure was in the Electron/node-hid/HIDAPI read lifecycle. The
working replacement gives one long-lived native C++ process sole ownership of
the interface through Linux hidraw. WSL is a practical isolation boundary and
also avoids Windows application-control trouble with an unsigned helper.

### Can COUGAR LCD Editor run at the same time?

No. Two clients racing for the same HID interface can corrupt transfers or
reproduce lifecycle problems. Exit COUGAR LCD Editor before attaching the LCD
to WSL.

### Is the Windows probe safe?

Yes. It opens HID interfaces with zero access for enumeration or
`GENERIC_READ` for optional logging. There is no `WriteFile`, output-report, or
feature-report operation in that target.

### Why is CPU temperature blank?

Windows does not expose a universal CPU-temperature API. Install the AMD bridge
for supported Ryzen systems, or contribute another opt-in provider. Never load
vendor kernel drivers from WSL.

### Why is CPU load different from Task Manager?

Without AMD CSV data it measures WSL's `/proc/stat`, not total Windows host
load. When AMD CSV is live, the dashboard uses the SDK's per-core C0 residency
as a host-load estimate.

### Can I use my own background or animation?

`--upload` accepts your own PNG. The built-in live view is generated in code.
Video and arbitrary animation formats have not been protocol-validated and are
not implemented.

### Does the service keep growing CSV files forever?

The AMD sample logger creates long-running CSV output. Stop/restart its task
periodically or archive/delete old CSV files while the logger is stopped. The
LCD client always chooses the newest fresh matching file.

### The display is blank after reboot—what should I check?

Run `usbipd list`, then inspect the WSL service and journal commands shown
above. Most reboot failures are a missing USB attachment, a renamed WSL distro,
or the LCD still being owned by COUGAR LCD Editor.

## Documentation

- [Protocol notes](docs/PROTOCOL.md)
- [Troubleshooting](docs/TROUBLESHOOTING.md)
- [Contributing](CONTRIBUTING.md)
- [Security policy](SECURITY.md)

## License

MIT. See [LICENSE](LICENSE). Product names and trademarks belong to their
respective owners. No COUGAR or AMD assets are distributed.
