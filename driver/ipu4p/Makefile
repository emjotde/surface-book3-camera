# Intel IPU4P (Ice Lake, 8086:8a19) driver, forked from the mainline
# IPU6 driver and retargeted with IPU4 hardware data from the
# intel/linux-intel-lts 4.19 tree.

# select the IPU4P branches in the 4.19-derived ABI headers
ccflags-y += -DCONFIG_VIDEO_INTEL_IPU4P=1

intel-ipu4p-objs := ipu4p.o \
		    ipu4p-bus.o \
		    ipu4p-dma.o \
		    ipu4p-mmu.o \
		    ipu4p-buttress.o \
		    ipu4p-cpd.o \
		    ipu4p-fw-com.o
obj-m += intel-ipu4p.o

intel-ipu4p-isys-objs := ipu4p-isys.o \
			 ipu4p-fw-isys.o \
			 ipu4p-isys-csi2.o \
			 ipu4p-isys-subdev.o \
			 ipu4p-isys-video.o \
			 ipu4p-isys-queue.o
obj-m += intel-ipu4p-isys.o

KDIR ?= /lib/modules/$(shell uname -r)/build

all:
	$(MAKE) -C $(KDIR) M=$(CURDIR) modules

clean:
	$(MAKE) -C $(KDIR) M=$(CURDIR) clean
