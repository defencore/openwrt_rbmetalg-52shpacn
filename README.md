# OpenWrt 25.12.4 for MikroTik Metal 52 ac (RBMetalG-52SHPacn)

<img width="3692" height="1063" alt="1190_hi_res" src="https://github.com/user-attachments/assets/1707d9e1-7594-4b76-bc65-3e24bccce30f" />
https://mikrotik.com/product/RBMetalG-52SHPacn
https://openwrt.org/inbox/toh/mikrotik/rbmetalg-52shpacn


## Specification
<img width="1199" height="1133" alt="image" src="https://github.com/user-attachments/assets/de0e7103-1c74-4c35-8c14-87f284ad56a7" />

### Block Diagram
<img width="400" alt="image" src="https://github.com/user-attachments/assets/a1197f0c-6433-4d19-af26-fbf883d3372e" />

## Wireless Specification
<img width="1166" height="378" alt="image" src="https://github.com/user-attachments/assets/a1dfe8cd-0ec4-4681-bb32-c24256ab2ac2" />



---
## 1. Host dependencies

``` bash
# ubuntu 24.04.4 server
sudo apt update
sudo apt install build-essential clang flex bison g++ gawk \
  gcc-multilib g++-multilib gettext git libncurses-dev libssl-dev \
  python3-setuptools rsync swig unzip zlib1g-dev file wget
```
---

## 2. Get the exact 25.12.4 tree
``` bash
# openwrt v25.12.4
git clone https://github.com/openwrt/openwrt
cd openwrt
git checkout v25.12.4    # pin to the release, not main
```

---
## 3. Apply this bundle

``` bash
git clone https://github.com/defencore/openwrt_rbmetalg-52shpacn
metal_52ac=$(realpath openwrt_rbmetalg-52shpacn/)


git checkout -- \
  target/linux/ath79/image/mikrotik.mk \
  target/linux/ath79/mikrotik/base-files/etc/hotplug.d/firmware/11-ath10k-caldata \
  target/linux/ath79/mikrotik/base-files/etc/board.d/01_leds \
  target/linux/ath79/mikrotik/base-files/etc/board.d/02_network
find target/linux/ath79 -name '*.rej' -delete
find target/linux/ath79 -name '*.orig' -delete

# drop-in files (DTS, kernel patch, init scripts, hotplug hook)
cp -r ${metal_52ac}/files/. .

chmod a+x target/linux/ath79/mikrotik/base-files/etc/init.d/bootbeep
chmod a+x target/linux/ath79/mikrotik/base-files/usr/bin/rbmetal-health

# tree patches (device entry, caldata, LEDs, network)
for p in ${metal_52ac}/patches/*.patch; do
    patch -p1 < "$p"
done
```

During the loop `patch` may print warnings like `Hunk #N succeeded ... with
fuzz 1` or `patch unexpectedly ends in middle of line`. These are harmless:
`succeeded` means the hunk applied (fuzz = the context shifted a line and
patch realigned it), and the "middle of line" note only means a patch file
has no trailing newline. The real success check is that no `.rej` files were
produced:
``` bash
find . -name '*.rej'
```
This must print nothing. If you need to re-run the loop, first re-do the
`git checkout -- ...` / `*.rej` cleanup / `cp -r files/.` steps above so the
patches apply to clean files — otherwise `patch` will detect the already
applied hunks and prompt to reverse them.

---
## 4. Configure and build
``` bash
./scripts/feeds clean
./scripts/feeds update -a
./scripts/feeds install -a

make menuconfig
#   Target System            -> Atheros ATH79
#   Subtarget                -> MikroTik devices
#   Target Profile           -> MikroTik RouterBOARD Metal 52 ac
#   Kernel modules  --->
#     SPI Support  --->
#       <*> kmod-spi-dev
#     Wireless Drivers  --->
#       [*] Enable LED support
#       < > kmod-ath9k
#   Utilities  --->
#     <*> spi-tools

# force the DTB to rebuild after DTS/patch changes
make target/linux/clean
# on error, for a readable log: make -j1 V=s
make -j$(nproc)
```

Output in `bin/targets/ath79/mikrotik/`:
- `openwrt-ath79-mikrotik-mikrotik_routerboard-metal-52ac-initramfs-kernel.bin` - netboot (RAM) image.
- `openwrt-ath79-mikrotik-mikrotik_routerboard-metal-52ac-squashfs-sysupgrade.bin` - the flash image

---
## 5. Netboot the initramfs image
Set a static IP in 192.168.1.0/24 on the wired interface (here enp0s8),
connect the router port directly, then:

``` bash
cd bin/targets/ath79/mikrotik/
sudo dnsmasq -d -p0 -i enp0s8 --bind-interfaces \
  --dhcp-authoritative --bootp-dynamic \
  -F 192.168.1.100,192.168.1.200 \
  --dhcp-boot=openwrt-ath79-mikrotik-mikrotik_routerboard-metal-52ac-initramfs-kernel.bin \
  --enable-tftp --tftp-root="$(pwd)"
```
In RouterOS, arm a one-shot network boot:
``` bash
/system routerboard settings set boot-device=try-ethernet-once-then-nand
```
Power-cycle the router (or hold Reset until the file is sent). OpenWrt comes up in RAM at 192.168.1.1.

---
## 6. Flash
Dropbear in 25.12 has no sftp-server, so use scp -O:
``` bash
scp -O openwrt-ath79-mikrotik-mikrotik_routerboard-metal-52ac-squashfs-sysupgrade.bin root@192.168.1.1:/tmp/
ssh root@192.168.1.1
sysupgrade -n /tmp/openwrt-ath79-mikrotik-mikrotik_routerboard-metal-52ac-squashfs-sysupgrade.bin
```

---
## 7. Health monitoring (input voltage + temperature)

The board carries a **ZillTek ZT2046Q** (ADS7846/XPT2046-compatible 12-bit SPI
ADC) that MikroTik uses for voltage/temperature sensing. It is *not* on its own
SPI bus: it shares SCLK/MOSI/MISO with the boot NOR flash and its `/CS` is the
SoC's **native SPI_CS2**, which the bootloader muxes onto **GPIO pin 17**
(confirmed live via `GPIO_OUT_FUNCTION` and against the stock RouterOS kernel,
board code `g52c`). SPI_CS1 is muxed to no pin — that is why the ADC must be on
chip-select **2**, not 1.

The DTS therefore declares `num-cs = <3>` and `adc@2 { reg = <2> }`, so the ADC
enumerates as **`/dev/spidev0.2`** (bound to spidev via `rohm,dh2228fv`).

> The full reverse-engineering write-up (with diagrams) — how the chip was
> identified, why `reg = <1>` read zeros, and how it was calibrated — is in
> [`docs/HOWTO-zt2046q-health.md`](docs/HOWTO-zt2046q-health.md). The pinmux
> dumper used to find the chip-select pin is in
> [`docs/tools/adc-diag/`](docs/tools/adc-diag/).

Read it with the bundled helper:

``` bash
rbmetal-health        # voltage:     24.00 V
                      # temperature: 38 C
rbmetal-health -j     # {"voltage":24.00,"temperature":38,"raw":{...}}
rbmetal-health -r     # TEMP0=1024 AUX=2438 TEMP1=1236
```

Channel map on this board (single-ended control bytes, mode 0):

| ctrl | channel | use |
|------|---------|-----|
| `0xE7` | AUX | input voltage, via an on-board ~16:1 divider (full scale ~40 V vs the ADC's 2.5 V reference) |
| `0x87` / `0xF7` | TEMP0 / TEMP1 | board temperature, ADC internal-diode delta (TEMP1 − TEMP0) |

**Three things are essential, otherwise the readings are garbage:**

1. **`reg = <2>`** (native SPI_CS2). On `reg = <1>` the controller toggles an
   unrouted CS and `/dev/spidev0.1` reads all-zeros.
2. **All channels of a sample must be clocked in one transfer with `/CS` held
   low.** One `spi-pipe` call per channel toggles `/CS` and resets the ADC's
   sample-and-hold; the readings then collapse to a common value. `rbmetal-health`
   sends the whole control-byte sequence in a single transfer.
3. **Discard the first conversion of each frame.** The first sample after `/CS`
   goes low has not settled, so the frame starts with a throwaway conversion and
   the real channels are read from the groups after it.

Calibration (validated at two points each):
- **Voltage** — a two-point linear fit of AUX against bench supplies, set in the
  script via `VCAL1_*`/`VCAL2_*`: **12.0 V → raw 1240**, **24.0 V → raw 2438**.
  Reads 12.00 V and 24.00 V at those points; the small offset is why two points
  beat a single through-zero scale.
- **Temperature** — the ZT2046Q is an ADS7846 *clone*; its diode constants
  differ from TI's, so the datasheet formula gives nonsense (it read ~62 °C at a
  real ~39 °C). The diode delta is proportional to absolute temperature (PTAT),
  so it is scaled from one point: `TEMP_CAL_DT=213` / `TEMP_CAL_TK=312` (~39 °C).
  Confirmed with a fridge test — at a ~25 °C case the delta fell to ~203, which
  the same scale reports as ~26 °C (the ADC die runs a couple degrees above the
  case). Re-measure once with a known temperature to refine.
