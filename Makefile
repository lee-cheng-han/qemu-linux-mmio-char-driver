.PHONY: help check

help:
	@echo "Available targets:"
	@echo "  make check    Run local repository hygiene checks"

check:
	test -f README.md
	test -f LICENSE
	test -f Makefile
	test -f MAINTAINERS
	test -f .clang-format
	test -f docs/architecture.md
	test -f docs/REGISTERS.md
	test -f docs/concurrency.md
	test -f docs/register_map.md
	test -f docs/driver_api.md
	test -f docs/qemu_device.md
	test -f docs/testing.md
	test -f docs/bringup.md
	test -f docs/demo.md
	test -f qemu/hw/misc/qemu_mbox.c
	test -f qemu/include/hw/misc/qemu_mbox.h
	test -f qemu/patches/README.md
	test -f qemu/patches/meson.build.fragment
	test -f qemu/patches/Kconfig.fragment
	test -f qemu/patches/aarch64-softmmu.default.mak.fragment
	test -f qemu/patches/qtest-meson.build.fragment
	test -f qemu/tests/qtest/qemu_mbox-test.c
	test -f kernel/Documentation/devicetree/bindings/misc/virt,mbox.yaml
	test -f kernel/drivers/misc/vmbox.c
	test -f kernel/drivers/misc/Kconfig.fragment
	test -f kernel/drivers/misc/Makefile.fragment
	test -f kernel/include/uapi/linux/vmbox.h
	test -f scripts/udev/99-vmbox.rules
	test -f tests/fuzz/README.md
	grep -q "SPDX-License-Identifier" qemu/hw/misc/qemu_mbox.c
	grep -q "SPDX-License-Identifier" qemu/include/hw/misc/qemu_mbox.h
	grep -q "SPDX-License-Identifier" qemu/tests/qtest/qemu_mbox-test.c
	grep -q "SPDX-License-Identifier" docs/REGISTERS.md
	grep -q "SPDX-License-Identifier" kernel/Documentation/devicetree/bindings/misc/virt,mbox.yaml
	grep -q "SPDX-License-Identifier" kernel/drivers/misc/vmbox.c
	grep -q "SPDX-License-Identifier" kernel/include/uapi/linux/vmbox.h
	grep -q "SPDX-License-Identifier" scripts/udev/99-vmbox.rules
	@if git grep -n '[[:blank:]]$$' -- '*.c' '*.h' '*.md' '*.yml' '*.yaml' '*.fragment' '*.rules'; then \
		echo "Trailing whitespace found"; \
		exit 1; \
	fi
	@if git grep -n "$$(printf '\t')" -- '*.md' '*.yml' '*.yaml' '*.fragment' '*.rules'; then \
		echo "Tab characters found in Markdown/YAML"; \
		exit 1; \
	fi
	git diff --check
