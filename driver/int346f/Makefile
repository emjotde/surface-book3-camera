obj-m += int346f.o
int346f-y := discrete.o common.o clk_and_regulator.o led.o discrete_quirks.o

KDIR ?= /lib/modules/$(shell uname -r)/build

all:
	$(MAKE) -C $(KDIR) M=$(CURDIR) modules

clean:
	$(MAKE) -C $(KDIR) M=$(CURDIR) clean
