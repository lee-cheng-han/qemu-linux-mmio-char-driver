#!/bin/sh
# SPDX-License-Identifier: MIT
set -eu

usage() {
    echo "usage: $0 /path/to/qemu-checkout" >&2
}

if [ "$#" -ne 1 ]; then
    usage
    exit 2
fi

qemu_tree="$1"
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

    printf '\n# virt-mbox integration from qemu-linux-mmio-char-driver\n' >> "$target"
    sed '/^# Add to /d;/^# Optional local-development entry/d' "$fragment" >> "$target"
    echo "updated: $target"
}

need_dir "$qemu_tree/hw/misc"
need_dir "$qemu_tree/include/hw/misc"
need_dir "$qemu_tree/tests/qtest"

cp "$repo_root/qemu/hw/misc/qemu_mbox.c" "$qemu_tree/hw/misc/qemu_mbox.c"
cp "$repo_root/qemu/include/hw/misc/qemu_mbox.h" \
   "$qemu_tree/include/hw/misc/qemu_mbox.h"
cp "$repo_root/qemu/tests/qtest/qemu_mbox-test.c" \
   "$qemu_tree/tests/qtest/qemu_mbox-test.c"

append_if_missing "qemu_mbox.c" \
    "$repo_root/qemu/patches/meson.build.fragment" \
    "$qemu_tree/hw/misc/meson.build"
append_if_missing "config QEMU_MBOX" \
    "$repo_root/qemu/patches/Kconfig.fragment" \
    "$qemu_tree/hw/misc/Kconfig"
append_if_missing "CONFIG_QEMU_MBOX" \
    "$repo_root/qemu/patches/aarch64-softmmu.default.mak.fragment" \
    "$qemu_tree/configs/devices/aarch64-softmmu/default.mak"

if [ -f "$qemu_tree/tests/qtest/meson.build" ]; then
    append_if_missing "qemu_mbox-test" \
        "$repo_root/qemu/patches/qtest-meson.build.fragment" \
        "$qemu_tree/tests/qtest/meson.build"
else
    echo "skipped qtest meson fragment: tests/qtest/meson.build not found" >&2
fi

cat <<EOF
QEMU payload applied to: $qemu_tree

Next:
  cd "$qemu_tree"
  mkdir -p build
  cd build
  ../configure --target-list=aarch64-softmmu
  ninja
EOF
