# SPDX-License-Identifier: GPL-2.0
#
# Top-level wrapper for out-of-tree builds and DKMS.

KVER ?= $(shell uname -r)
KDIR ?= /lib/modules/$(KVER)/build
KERNEL_DIR ?= kernel

# Adding utility targets above `all` must not change the DKMS/plain-make contract.
.DEFAULT_GOAL := all

# Expand only for DKMS targets: plain module builds may omit dkms.conf.
DKMS_PACKAGE_NAME = $(shell awk -F '"' '/^PACKAGE_NAME=/ { print $$2; exit }' dkms.conf)
DKMS_PACKAGE_VERSION = $(shell awk -F '"' '/^PACKAGE_VERSION=/ { print $$2; exit }' dkms.conf)

.PHONY: all clean modules modules_install help dkms-add dkms-build dkms-install dkms-remove test

# Host regressions only: no root, kernel headers, GPU or attached peer required.
test:
	python3 tools/ci/run-smoke.py $(if $(TEST_OUTPUT),--output "$(TEST_OUTPUT)")

all: modules

modules:
	$(MAKE) -C $(KERNEL_DIR) KVER=$(KVER) KDIR=$(KDIR) modules

modules_install:
	$(MAKE) -C $(KERNEL_DIR) KVER=$(KVER) KDIR=$(KDIR) modules_install

clean:
	$(MAKE) -C $(KERNEL_DIR) KVER=$(KVER) KDIR=$(KDIR) clean

help:
	$(MAKE) -C $(KERNEL_DIR) help
	@echo ""
	@echo "Top-level targets:"
	@echo "  test              Run CPU regressions without root or hardware"
	@echo "  dkms-add          Register this source tree with DKMS"
	@echo "  dkms-build        Build with DKMS for KVER=$$(uname -r)"
	@echo "  dkms-install      Install with DKMS for KVER=$$(uname -r)"
	@echo "  dkms-remove       Remove this DKMS module version"

dkms-add:
	dkms add .

dkms-build:
	dkms build "$(DKMS_PACKAGE_NAME)/$(DKMS_PACKAGE_VERSION)" -k "$(KVER)"

dkms-install:
	dkms install "$(DKMS_PACKAGE_NAME)/$(DKMS_PACKAGE_VERSION)" -k "$(KVER)"

dkms-remove:
	dkms remove "$(DKMS_PACKAGE_NAME)/$(DKMS_PACKAGE_VERSION)" --all
