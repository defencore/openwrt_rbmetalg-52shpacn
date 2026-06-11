# adc-diag — SPI/GPIO pinmux dump

A tiny, read-only kernel module that prints the ath79 SPI controller registers
and decodes the GPIO output-function mux, so you can see which pin carries which
SPI signal. It is what located the ZT2046Q ADC's chip-select on this board.

## Build

It must be built against the **exact** kernel that runs on the router, using
the OpenWrt toolchain:

```sh
OW=~/openwrt                       # your OpenWrt 25.12.4 tree (see top-level README)
export STAGING_DIR=$OW/staging_dir
export PATH=$STAGING_DIR/toolchain-mips_24kc_gcc-14.3.0_musl/bin:$PATH
KDIR=$OW/build_dir/target-mips_24kc_musl/linux-ath79_mikrotik/linux-6.12.87

make KDIR=$KDIR
```

This produces `adc-diag.ko`.

## Run

```sh
scp -O adc-diag.ko root@192.168.1.1:/tmp/
ssh root@192.168.1.1
insmod /tmp/adc-diag.ko        # init returns an error on purpose -> auto-unloads
dmesg | grep adc-diag
```

## Expected output on the Metal 52 ac

```
adc-diag: SPI  FS=00000001 CTRL=00000042 IOC=00070000 RDS=ffffffff
adc-diag: GPIO OE=00f8ab0b IN=200eae3f OUT=00040810 IN_ENABLE0=00000908
adc-diag: GPIO5  output = func 0x09 = SPI_CS0
adc-diag: GPIO6  output = func 0x08 = SPI_CLK
adc-diag: GPIO7  output = func 0x0c = SPI_MOSI
adc-diag: GPIO17 output = func 0x0b = SPI_CS2
adc-diag: SPI_DATA_IN (MISO) <- GPIO8
```

Reading: the NOR flash is on CS0/CLK/MOSI (pins 5/6/7) with MISO on GPIO8;
**the ADC's chip-select is the controller's native SPI_CS2 on pin 17**, and
SPI_CS1 is muxed to no pin. That is the whole reason the device tree puts the
ADC on `reg = <2>`. See `../../HOWTO-zt2046q-health.md` for the full story.

It changes nothing — every register is only read. Safe to load on a running
system.
