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

## Layout

- `src/linux/`: WSL renderer, telemetry, and HID transport
- `tools/windows/`: native read-only enumerator/logger
- `scripts/`: build, install, startup, and optional AMD bridge
- `packaging/systemd/`: WSL service
- `docs/PROTOCOL.md`: evidence boundary and wire format

