# AGENTS.md

## Scope

Asset-free, unofficial C++ client for the COUGAR CFV235-series 9.16-inch LCD:
`VID_1D6B/PID_0126`, usage `FF00:0001`, 1025-byte HID reports.

## Safety invariants

- Never target `VID_264A/PID_22C5`; it is unrelated ThermalTake hardware.
- Keep `cougar-hid-probe` strictly read-only. Do not add `WriteFile`, output
  reports, feature reports, firmware, storage erase, or guessed commands.
- Live writes are limited to captured/validated `brightness`, `power/resume`,
  `transport`, media blocks, and `transported`.
- Never commit device serials, captures, logs, CSVs, firmware, vendor binaries,
  fonts, images, or extracted COUGAR/AMD application content.
- Do not run the live client concurrently with COUGAR LCD Editor.
- Never delete, rename, patch, or redistribute SignalRGB/HWiNFO binaries or
  drivers. The supported coexistence mechanism is reversible task ordering.

## SignalRGB/HWiNFO ordering invariant

On the tested Ryzen host, SignalRGB hardware monitoring loaded an `HWiNFO_*`
driver before AMD telemetry and made the AMD SDK fail. The workaround keeps
SignalRGB lighting but disables its monitoring/fan-sensor settings.

`Start-SensorsInOrder.ps1` must preserve this sequence:

1. Stop SignalRGB UI/service and wait for stale `HWiNFO_*` drivers to stop.
2. Start the Authenticode-validated AMD logger with `CreateNoWindow`.
3. Require a fresh valid AMD CSV temperature row.
4. Start SignalRGB service/UI with monitoring disabled.
5. Verify that the CSV still advances after SignalRGB starts.

`Install-SignalRgbCoexistence.ps1` must back up every setting it changes,
disable the standalone AMD task to avoid a race, and register only one hidden
elevated logon task. `Uninstall-SignalRgbCoexistence.ps1` must restore the
backup. Do not weaken signature validation or continue when HWiNFO is stuck in
`Stop Pending`.

## Build and verify

Linux/WSL:

```bash
cmake -S . -B build/wsl -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build/wsl
ctest --test-dir build/wsl --output-on-failure
./build/wsl/src/linux/cougar-lcd --render-only /tmp/smoke.png
```

Windows (VS Build Tools shell or PowerShell with CMake available):

```powershell
.\scripts\windows\Build.ps1
```

Run hardware writes only when explicitly requested, the COUGAR editor is
closed, and `--probe` confirms `1d6b:0126`. Keep install scripts unattended,
idempotent, path-independent, and free of visible console windows at logon.
Keep the systemd `ExecStartPre` USB visibility check: Windows may report a stale
Attached state after WSL restarts even though no HID interface exists in WSL.
Keep the Windows launcher task's hidden `-KeepAlive` loop and unlimited task
duration; otherwise WSL may stop the dashboard when no terminal is open.

## Layout

- `src/linux/`: WSL renderer, telemetry, and HID transport
- `tools/windows/`: native read-only enumerator/logger
- `scripts/`: build, install, startup, and optional AMD bridge
- `scripts/windows/*SignalRgb*` and `Start-SensorsInOrder.ps1`: reversible
  AMD-first/SignalRGB-last coexistence setup
- `packaging/systemd/`: WSL service
- `docs/PROTOCOL.md`: evidence boundary and wire format
