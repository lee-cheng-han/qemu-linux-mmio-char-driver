.PHONY: help check

help:
	@echo "Available targets:"
	@echo "  make check    Run local repository hygiene checks"

check:
	test -f README.md
	test -f LICENSE
	test -f Makefile
	test -f docs/architecture.md
	test -f docs/register_map.md
	test -f docs/driver_api.md
	test -f docs/qemu_device.md
	test -f docs/testing.md
	test -f docs/bringup.md
	test -f docs/demo.md
	test -f qemu/hw/misc/qemu_mbox.c
	test -f qemu/include/hw/misc/qemu_mbox.h
	grep -q "SPDX-License-Identifier" qemu/hw/misc/qemu_mbox.c
	grep -q "SPDX-License-Identifier" qemu/include/hw/misc/qemu_mbox.h
	@if git grep -n '[[:blank:]]$$' -- '*.c' '*.h' '*.md' '*.yml' '*.yaml'; then \
		echo "Trailing whitespace found"; \
		exit 1; \
	fi
	@if git grep -n "$$(printf '\t')" -- '*.md' '*.yml' '*.yaml'; then \
		echo "Tab characters found in Markdown/YAML"; \
		exit 1; \
	fi
	git diff --check
