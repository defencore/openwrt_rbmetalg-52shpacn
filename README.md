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

# drop-in files (DTS, kernel patch, init scripts, hotplug hook)
cp -r ${metal_52ac}/files/. .

# tree patches (device entry, caldata, LEDs, network)
for p in ${metal_52ac}/patches/*.patch; do
    patch -p1 < "$p"
done
```

Verify the patches applied with no .rej files:
``` bash
find . -name '*.rej'
```

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
#     Wireless Drivers  --->
#       [*] Enable LED support
#       < > kmod-ath9k

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
