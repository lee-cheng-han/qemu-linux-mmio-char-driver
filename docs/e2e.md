<!-- SPDX-License-Identifier: MIT -->

# End-To-End Bring-Up

This repository contains the project-owned QEMU device, Linux driver source,
userspace tests, and integration fragments. The real end-to-end run happens
after applying those fragments to full QEMU and Linux source trees.

## Expected Flow

```text
QEMU boots Linux
QEMU exposes virt-mbox as an MMIO platform device
Linux probes the vmbox driver
/dev/vmbox0 appears
userspace selftests pass
debugfs/sysfs state is readable
```

## Helper Scripts

From this repository:

```sh
scripts/apply-qemu.sh ~/work/qemu
scripts/apply-linux.sh ~/work/linux
scripts/build-userspace.sh /tmp/vmbox_test
scripts/run-e2e-checklist.sh
```

The apply scripts are intentionally conservative: they copy source payloads and
append integration fragments only when the target checkout does not already
contain the expected marker.

## QEMU Command Shape

```sh
qemu-system-aarch64 \
  -machine virt \
  -cpu cortex-a57 \
  -m 1024 \
  -kernel Image \
  -append "console=ttyAMA0 root=/dev/vda rw" \
  -drive if=none,file=rootfs.ext4,format=raw,id=hd0 \
  -device virtio-blk-device,drive=hd0 \
  -nographic
```

The final command line depends on the external QEMU machine wiring used for
`virt-mbox`.

## Guest Validation

```sh
modprobe vmbox
test -e /dev/vmbox0
cat /sys/bus/platform/devices/*mbox*/fifo_depth
cat /sys/bus/platform/devices/*mbox*/status
mount -t debugfs none /sys/kernel/debug || true
cat /sys/kernel/debug/vmbox0/stats
./vmbox_test
```

## Sanitizer Boot Notes

For runtime sanitizer coverage, boot a kernel with:

- `CONFIG_KASAN=y`
- `CONFIG_DEBUG_KMEMLEAK=y`
- `CONFIG_LOCKDEP=y`
- `CONFIG_PROVE_LOCKING=y`

After tests, scan:

```sh
dmesg | grep -Ei 'BUG|WARNING|KASAN|kmemleak|Oops|use-after-free'
cat /sys/kernel/debug/kmemleak
```
