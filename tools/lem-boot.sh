#!/bin/sh
# Boot a temporary image right after ABL enters fastboot: this phone drops out of
# fastboot ("press any key to shutdown") if the host is slow, so wait actively.
# usage: lem-boot.sh IMG [--wait-adb]
IMG=${1:?image}
if [ "$2" = "--wait-adb" ]; then
	echo "waiting for Android (adb)..." >&2
	adb wait-for-device || exit 1
	adb reboot bootloader
fi
echo "waiting for fastboot..." >&2
until fastboot devices 2>/dev/null | grep -q fastboot; do sleep 0.1; done
echo "sending $IMG" >&2
exec fastboot boot "$IMG"
