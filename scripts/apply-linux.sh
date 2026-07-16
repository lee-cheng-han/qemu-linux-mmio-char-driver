#!/bin/sh
# SPDX-License-Identifier: MIT
set -eu

usage() {
    echo "usage: $0 /path/to/linux-checkout" >&2
}

if [ "$#" -ne 1 ]; then
    usage
    exit 2
fi

linux_tree="$1"
repo_root="$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)"

need_file() {
    if [ ! -f "$1" ]; then
        echo "missing required file: $1" >&2
        exit 1
    fi
}

need_dir() {
    if [ ! -d "$1" ]; then
        echo "missing required directory: $1" >&2
        exit 1
    fi
}

append_if_missing() {
    marker="$1"
    fragment="$2"
    target="$3"

    need_file "$fragment"
    need_file "$target"

    if grep -q "$marker" "$target"; then
        echo "already present: $marker in $target"
        return
    fi

    printf '\n# vmbox integration from qemu-linux-mmio-char-driver\n' >> "$target"
    sed '/^# SPDX-License-Identifier/d' "$fragment" >> "$target"
    echo "updated: $target"
}

need_dir "$linux_tree/drivers/misc"
need_dir "$linux_tree/include/uapi/linux"
need_dir "$linux_tree/Documentation/devicetree/bindings/misc"

cp "$repo_root/kernel/drivers/misc/vmbox.c" \
   "$linux_tree/drivers/misc/vmbox.c"
cp "$repo_root/kernel/include/uapi/linux/vmbox.h" \
   "$linux_tree/include/uapi/linux/vmbox.h"
cp "$repo_root/kernel/Documentation/devicetree/bindings/misc/virt,mbox.yaml" \
   "$linux_tree/Documentation/devicetree/bindings/misc/virt,mbox.yaml"

append_if_missing "config VMBOX" \
    "$repo_root/kernel/drivers/misc/Kconfig.fragment" \
    "$linux_tree/drivers/misc/Kconfig"
append_if_missing "vmbox.o" \
    "$repo_root/kernel/drivers/misc/Makefile.fragment" \
    "$linux_tree/drivers/misc/Makefile"

cat <<EOF
Linux payload applied to: $linux_tree

Next:
  cd "$linux_tree"
  make menuconfig   # enable CONFIG_VMBOX=m or y
  make modules
EOF
