MODULES := engine.o

#guest architecture
ARCH := arm
CROSS_COMPILE := ../tools/arm-bcm2708/arm-bcm2708hardfp-linux-gnueabi/bin/arm-bcm2708hardfp-linux-gnueabi-


ROOTDIR := ../kernel3.6

obj-m := $(MODULES)

MAKEARCH := $(MAKE) ARCH=$(ARCH) CROSS_COMPILE=$(CROSS_COMPILE)

all: modules
modules:
	$(MAKEARCH) -C $(ROOTDIR) M=${shell pwd} modules

clean:
	$(MAKEARCH) -C $(ROOTDIR) M=${shell pwd} clean
