# QEMU Integration Notes

This directory contains the project-owned integration notes and build fragments
for adding `virt-mbox` to a real QEMU source tree.

The repository is not itself a full QEMU checkout. Treat the files under
`qemu/hw/` and `qemu/include/` as the source payload for a future QEMU patch.

## Source Payload

Copy these project files into the matching paths in a QEMU checkout:

```text
qemu/hw/misc/qemu_mbox.c          -> hw/misc/qemu_mbox.c
qemu/include/hw/misc/qemu_mbox.h  -> include/hw/misc/qemu_mbox.h
```

## Build-System Edits

Add the Meson entry from:

```text
qemu/patches/meson.build.fragment
```

to the QEMU checkout file:

```text
hw/misc/meson.build
```

Add the Kconfig entry from:

```text
qemu/patches/Kconfig.fragment
```

to the QEMU checkout file:

```text
hw/misc/Kconfig
```

For local compile testing, enable the device for one softmmu target by adding:

```text
qemu/patches/aarch64-softmmu.default.mak.fragment
```

to the QEMU checkout file:

```text
configs/devices/aarch64-softmmu/default.mak
```

That `default.mak` edit is a development convenience, not the final upstream
shape. The long-term path should select `QEMU_MBOX` from the machine or QTest
configuration that instantiates the device.

## Expected Patch Shape

The compile-integration patch should contain:

```text
configs/devices/aarch64-softmmu/default.mak
hw/misc/Kconfig
hw/misc/meson.build
hw/misc/qemu_mbox.c
include/hw/misc/qemu_mbox.h
```

The first QTest patch should later add:

```text
tests/qtest/meson.build
tests/qtest/qemu_mbox-test.c
```

This first integration step is compile-oriented. Runtime instantiation is a
separate step because a `SysBusDevice` also needs a machine or test harness to
map its MMIO region and wire its IRQ line.

## Apply Checklist

From the root of a QEMU checkout:

```sh
cp /path/to/this/repo/qemu/hw/misc/qemu_mbox.c hw/misc/qemu_mbox.c
cp /path/to/this/repo/qemu/include/hw/misc/qemu_mbox.h include/hw/misc/qemu_mbox.h
```

Then edit:

```text
hw/misc/meson.build
hw/misc/Kconfig
configs/devices/aarch64-softmmu/default.mak
```

and paste the matching fragment contents from this directory.

Or use the repository helper from this repository root:

```sh
scripts/apply-qemu.sh /path/to/qemu-checkout
```

## Minimal Build Check

From a QEMU checkout with the payload and build-system edits applied:

```sh
mkdir -p build
cd build
../configure --target-list=aarch64-softmmu
ninja
```

Any softmmu target can compile the device once `CONFIG_QEMU_MBOX` is selected
by a machine or temporary local config. The optional `default.mak` fragment does
that for `aarch64-softmmu`.

## Runtime Instantiation Plan

Later milestones should instantiate the device from one of these places:

- a small QTest-only machine
- a development machine hook
- an existing machine while prototyping

The instantiation code must:

- create the `virt-mbox` device
- realize it as a sysbus device
- map its MMIO region at the documented base address
- connect its IRQ line once IRQ support exists

Runtime instantiation should be added in a separate patch from the initial
compile integration so each step stays reviewable.

## QTest Skeleton

This repository includes a QTest skeleton at:

```text
qemu/tests/qtest/qemu_mbox-test.c
```

and the corresponding Meson fragment at:

```text
qemu/patches/qtest-meson.build.fragment
```

The test skeleton expects a future QEMU machine or QTest harness named
`virt-mbox-test-machine` that maps the device at `0x10000000`. Those names are
placeholders until runtime instantiation is added.
