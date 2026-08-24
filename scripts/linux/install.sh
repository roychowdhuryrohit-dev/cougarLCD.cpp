#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
build_dir="${repo_root}/build/wsl-release"

if ! command -v apt-get >/dev/null 2>&1; then
    echo "This installer currently supports Debian/Ubuntu WSL distributions." >&2
    exit 1
fi

sudo apt-get update
sudo apt-get install -y --no-install-recommends \
    build-essential cmake ninja-build pkg-config \
    libcairo2-dev libfontconfig1-dev libhidapi-dev fonts-dejavu-core usbutils

cmake -S "${repo_root}" -B "${build_dir}" -G Ninja \
    -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON
cmake --build "${build_dir}"
ctest --test-dir "${build_dir}" --output-on-failure
sudo cmake --install "${build_dir}"

sudo install -D -m 0644 \
    "${repo_root}/packaging/systemd/cougar-lcd.service" \
    /etc/systemd/system/cougar-lcd.service
sudo install -D -m 0755 \
    "${repo_root}/packaging/systemd/ensure-usb.sh" \
    /usr/local/libexec/cougar-lcd/ensure-usb.sh
if [[ ! -e /etc/default/cougar-lcd ]]; then
    printf '%s\n' 'COUGAR_LCD_ARGS="--quiet"' | \
        sudo tee /etc/default/cougar-lcd >/dev/null
fi
sudo systemctl daemon-reload
sudo systemctl enable cougar-lcd.service

echo
echo "Installed /usr/local/bin/cougar-lcd and enabled cougar-lcd.service."
echo "The Windows-side installer will attach USB and start the service."
