# Out-of-tree build of the adc-diag pinmux dump against the OpenWrt kernel.
#
# Point KDIR at the prepared kernel build dir inside your OpenWrt tree and
# put the matching toolchain on PATH, e.g.:
#
#   OW=~/openwrt
#   export STAGING_DIR=$OW/staging_dir
#   export PATH=$STAGING_DIR/toolchain-mips_24kc_gcc-*_musl/bin:$PATH
#   make KDIR=$OW/build_dir/target-mips_24kc_musl/linux-ath79_mikrotik/linux-*
#
# Then on the router: insmod adc-diag.ko ; dmesg | grep adc-diag

obj-m := adc-diag.o

KDIR ?= /lib/modules/$(shell uname -r)/build
ARCH ?= mips
CROSS_COMPILE ?= mips-openwrt-linux-

all:
	$(MAKE) -C $(KDIR) M=$(CURDIR) ARCH=$(ARCH) CROSS_COMPILE=$(CROSS_COMPILE) modules

clean:
	$(MAKE) -C $(KDIR) M=$(CURDIR) clean
