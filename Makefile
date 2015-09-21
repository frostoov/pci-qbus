# Makefile for ISA QBUS driver
# If KERNELRELEASE is defined, we've been invoked from the
# kernel build system and can use its language.

target ?= pci-qbus
obj-m = $(target).o

all:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD)

install: $(target).o
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) modules_install
clean:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) clean
