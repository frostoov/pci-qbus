# Makefile for ISA QBUS driver
# If KERNELRELEASE is defined, we've been invoked from the
# kernel build system and can use its language.

ifneq ($(KERNELRELEASE),)
	obj-m := pci-qbus.o
# Otherwise we were called directly from the command
# line; invoke the kernel build system.
else
	KERNELDIR ?= /lib/modules/$(shell uname -r)/build
	PWD := $(shell pwd)
default:
	$(MAKE) -C $(KERNELDIR) M=$(PWD) modules
endif

MODULEDIR = /lib/modules/$(shell uname -r)/qbus

install: pci-qbus.ko
	cp pci-qbus.ko $(MODULEDIR)/

clean:
	rm -f *.ko *~ core
