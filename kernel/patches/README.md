<!-- SPDX-License-Identifier: MIT -->

# Linux Integration Notes

This repository is not a full Linux kernel checkout. Treat the files under
`kernel/` as the source payload for a kernel integration patch.

## Source Payload

Copy these project files into the matching paths in a Linux checkout:

```text
kernel/drivers/misc/vmbox.c
kernel/include/uapi/linux/vmbox.h
kernel/Documentation/devicetree/bindings/misc/virt,mbox.yaml
```

Then merge these fragments into the kernel tree:

```text
kernel/drivers/misc/Kconfig.fragment
kernel/drivers/misc/Makefile.fragment
```

## Helper

From this repository root:

```sh
scripts/apply-linux.sh /path/to/linux-checkout
```

The helper copies the payload files and appends the Kconfig/Makefile fragments
only when the target checkout does not already contain the expected marker.

## Build Check

Inside the Linux checkout, enable:

```text
CONFIG_VMBOX=m
```

Then build modules:

```sh
make modules
```
