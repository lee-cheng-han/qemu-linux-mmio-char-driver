#!/bin/sh
# SPDX-License-Identifier: MIT
set -eu

repo_root="$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)"
out="${1:-/tmp/vmbox_test}"

cc="${CC:-cc}"
"$cc" -Wall -Wextra -O2 -I"$repo_root/kernel/include" \
    -o "$out" "$repo_root/tests/selftests/vmbox_test.c"

echo "built userspace test: $out"
