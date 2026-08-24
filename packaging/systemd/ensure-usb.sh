#!/bin/sh
set -eu

vid_pid="${COUGAR_LCD_USB_ID:-1d6b:0126}"
usbipd_exe="${COUGAR_LCD_USBIPD_EXE:-/mnt/c/Program Files/usbipd-win/usbipd.exe}"

device_is_visible() {
    lsusb -d "${vid_pid}" >/dev/null 2>&1
}

find_bus_id() {
    "${usbipd_exe}" list 2>/dev/null | tr -d '\r' | \
        awk -v id="${vid_pid}" '$2 == id && !found { print $1; found = 1 }'
}

attach_device() {
    bus_id="$(find_bus_id)"
    if [ -z "${bus_id}" ]; then
        return 1
    fi
    "${usbipd_exe}" attach --wsl --busid "${bus_id}" >/dev/null 2>&1 || true
}

wait_for_device() {
    remaining="${1:-10}"
    while [ "${remaining}" -gt 0 ]; do
        if device_is_visible; then
            return 0
        fi
        sleep 1
        remaining=$((remaining - 1))
    done
    return 1
}

if device_is_visible; then
    exit 0
fi

if [ ! -x "${usbipd_exe}" ]; then
    echo "usbipd-win was not found at ${usbipd_exe}." >&2
    exit 1
fi

# A normal attach is enough after Windows starts. After `wsl --shutdown`,
# usbipd-win can retain an Attached state even though the new WSL VM has no
# USB device. Verify from inside WSL and repair that stale state when needed.
attach_device || true
if wait_for_device 3; then
    exit 0
fi

"${usbipd_exe}" detach --hardware-id "${vid_pid}" >/dev/null 2>&1 || true
sleep 2
attach_device || true
if wait_for_device 10; then
    exit 0
fi

echo "COUGAR USB device ${vid_pid} could not be attached to WSL." >&2
exit 1
