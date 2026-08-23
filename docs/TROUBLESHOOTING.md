# Troubleshooting

## No display after reboot

In elevated PowerShell:

```powershell
usbipd list
Get-ScheduledTask -TaskName 'COUGAR LCD WSL Service'
wsl -d Ubuntu -- systemctl status cougar-lcd --no-pager
wsl -d Ubuntu -- journalctl -u cougar-lcd -n 100 --no-pager
```

Use your actual distro name. If `1d6b:0126` is not attached, run:

```powershell
.\scripts\windows\Start-CougarLcd.ps1 -Distro Ubuntu
```

Close COUGAR LCD Editor first. After a usbipd-win upgrade or physical USB port
change, an elevated `usbipd bind --hardware-id 1d6b:0126` may be needed again.

## `No VID_1D6B/PID_0126 HID interface is visible`

- Check that the LCD USB cable is connected.
- Run `usbipd list` on Windows.
- Confirm the device is attached to the intended WSL distro.
- Do not substitute a ThermalTake `264a:22c5` interface.
- Restart WSL with `wsl --shutdown`, reattach, and start the service.

## Static or stale metrics

Check the service log. NVIDIA metrics require WSL GPU support and a working
`/usr/lib/wsl/lib/libnvidia-ml.so.1`. Test with `nvidia-smi` inside WSL.

For AMD temperature, check from PowerShell:

```powershell
Get-Process AMDRyzenMasterMonitoringSampleApp -ErrorAction SilentlyContinue
Get-ChildItem C:\ProgramData\CougarLCD\RMSDK_Parameter_log_cougar_*.csv |
  Sort-Object LastWriteTime -Descending | Select-Object -First 1 Name,Length,LastWriteTime
```

The newest CSV must change at least every 15 seconds. The client deliberately
rejects stale data.

## AMD telemetry says `Failed to get the CPU Parameters`

AMD's monitoring driver may be blocked by another low-level sensor driver.
Completely exit HWiNFO, Fan Control, motherboard monitoring suites, Ryzen
Master, and SignalRGB hardware monitoring, then restart Windows and let AMD
telemetry start first.

SignalRGB lighting and the LCD can coexist, but SignalRGB's monitoring module
may not coexist with the AMD SDK on every system. Disable SignalRGB system/
hardware monitoring while keeping lighting enabled. This project does not
delete or patch SignalRGB DLLs because updates would restore them and binary
tampering is brittle.

For the tested task-ordering workaround, run from elevated PowerShell and then
restart Windows:

```powershell
.\scripts\windows\Install-SignalRgbCoexistence.ps1
Get-ScheduledTask -TaskName 'COUGAR LCD Sensor Startup Order'
Get-Content C:\ProgramData\CougarLCD\sensor-startup-order.log -Tail 50
```

The success condition is not merely that both processes exist. The ordered
task starts AMD first, waits for a valid CSV row, starts SignalRGB lighting,
then verifies that the CSV continues growing. If `HWiNFO_*` remains
`Stop Pending`, restart Windows and allow the ordered task to run before
opening any other hardware monitor.

## A blank `cmd.exe` window appears at sign-in

Re-run `Install.ps1`. Its scheduled action uses hidden, non-interactive
PowerShell. The optional AMD launcher uses `ProcessStartInfo.CreateNoWindow`
instead of keeping `cmd.exe` alive. Inspect other startup entries with:

```powershell
Get-CimInstance Win32_StartupCommand | Select-Object Name,Command,Location
Get-ScheduledTask | Where-Object TaskName -match 'COUGAR|AMD'
```

## Windows probe reports no device

The LCD can be owned by only one side at a time. Stop the WSL service and detach
the USB device before running the Windows probe:

```powershell
wsl -d Ubuntu -u root -- systemctl stop cougar-lcd
usbipd detach --hardware-id 1d6b:0126
```

Reattach it with `Start-CougarLcd.ps1` afterward.

## Upload fails midway

- Use a real PNG with a short ASCII filename (`A-Z`, `a-z`, digits, `.`, `_`,
  or `-`).
- Keep the file under 64 MB.
- Stop COUGAR LCD Editor and any second instance of `cougar-lcd`.
- Avoid detaching USB during the transfer.
- Let systemd restart the service if a transfer was interrupted.
