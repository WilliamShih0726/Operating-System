obj-m += kfetch_mod_313512009.o

KDIR ?= $(PWD)/linux-v6.8-devkit

all:
	$(MAKE) ARCH=$(ARCH) CROSS_COMPILE=$(CROSS_COMPILE) -C $(KDIR) M=$(PWD) modules
clean:
	$(MAKE) -C $(KDIR) M=$(PWD) clean
