#!/bin/sh
# SPDX-License-Identifier: MIT
set -eu

cat <<'EOF'
vmbox end-to-end checklist

1. Apply QEMU payload:
   scripts/apply-qemu.sh ~/work/qemu

2. Build QEMU:
   cd ~/work/qemu
   mkdir -p build
   cd build
   ../configure --target-list=aarch64-softmmu
   ninja

3. Apply Linux payload:
   scripts/apply-linux.sh ~/work/linux

4. Build Linux with CONFIG_VMBOX=m or CONFIG_VMBOX=y.

5. Boot the guest with the QEMU build that instantiates virt-mbox.

6. In the guest:
   modprobe vmbox
   test -e /dev/vmbox0
   cat /sys/bus/platform/devices/*mbox*/fifo_depth
   cat /sys/bus/platform/devices/*mbox*/status

7. Build and copy the userspace test:
   scripts/build-userspace.sh /tmp/vmbox_test

8. In the guest:
   ./vmbox_test
   mount -t debugfs none /sys/kernel/debug || true
   cat /sys/kernel/debug/vmbox0/stats
   cat /sys/kernel/debug/vmbox0/regs
   dmesg | grep -Ei 'BUG|WARNING|Oops|KASAN|kmemleak|use-after-free'
EOF
