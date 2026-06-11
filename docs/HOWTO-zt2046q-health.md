# Reverse-engineering the ZT2046Q health sensor (voltage + temperature)

How OpenWrt was made to read the input voltage and board temperature on the MikroTik RouterBOARD Metal 52 ac (RBMetalG-52SHPacn) — what the chip is, how it is wired, why the obvious device-tree entry read zeros, and how it was fixed and calibrated. The end result is the [`rbmetal-health`](../files/target/linux/ath79/mikrotik/base-files/usr/bin/rbmetal-health) script; this document is the story behind it.

---

## TL;DR

* The board carries a **ZillTek ZT2046Q**, a 12-bit SPI ADC that is a clone of the TI **ADS7846 / XPT2046** touch-screen controller. MikroTik uses it as the voltage/temperature monitor.
* It is **not** on its own bus. It shares `SCLK/MOSI/MISO` with the boot NOR flash and its `/CS` is the SoC SPI controller's **native SPI_CS2**, which the bootloader muxes onto **GPIO 17**. `SPI_CS1` is muxed to no pin.
* So the device tree must put it on **chip-select 2** (`reg = <2>`, `num-cs = <3>`). It then enumerates as `/dev/spidev0.2`. The original `reg = <1>` toggled an unrouted CS and read all-zeros.
* Reads must be done as **one SPI transfer with `/CS` held low for the whole frame**, and the **first conversion of each frame must be discarded**.
* Voltage is a two-point linear fit of the AUX channel; temperature is the ADC's internal-diode delta, scaled PTAT from one reference point.

```
                 QCA9556 SoC (ath79)
        ┌─────────────────────────────────────────┐
        │  boot SPI controller @0x1f000000        │
        │   FS / CTRL / IOC / RDS registers       │
        └───┬──────┬──────┬──────┬───────────┬────┘
            │CLK   │MOSI  │MISO  │CS0        │CS2
          pin6   pin7   pin8   pin5        pin17
            │      │      │      │           │
   ┌────────┼──────┼──────┼──────┤           │
   │        ▼      ▼      ▼      ▼           │
   │   ┌───────────────────────────┐         │     shared bus:
   │   │  NOR flash IS25LP128      │ /CS◄────┘     CLK + MOSI + MISO
   │   └───────────────────────────┘   (CS0)       common to both chips,
   │        ▲      ▲      ▲                        only /CS differs
   │   ┌───────────────────────┐
   └──►│  ZT2046Q  (ADS7846)   │ /CS◄────────┘
       │  AUX=Vin  TEMPx       │   (CS2 = GPIO17)
       └───────────────────────┘
```

---

## 1. The hardware

| Part | Role |
|------|------|
| Qualcomm Atheros **QCA9556** | SoC (ath79, MIPS 74Kc) — boot SPI controller + GPIO mux |
| ISSI IS25LP128 | 16 MB SPI NOR flash (boot) on chip-select 0 |
| **ZillTek ZT2046Q** | 12-bit SPI ADC (ADS7846/XPT2046 clone): voltage + temperature |
| AR8033 | gigabit Ethernet PHY (unrelated) |
| QCA9889 | Wi-Fi (unrelated) |

The only chips on the board are these plus the power section — **there is no shift register / CPLD**, so the ADC's chip-select is a direct SoC line, not a latch output.

The ZT2046Q speaks the ADS7846 protocol: each access is a single byte of command followed by the 12-bit result, MSB first, SPI mode 0.

```
 ADS7846 / ZT2046Q control byte
  bit:  7    6  5  4    3      2       1   0
       [S ] [A2 A1 A0] [MODE] [SER/-DFR] [PD1 PD0]
        │     │          │       │          └ power-down / reference
        │     │          │       └ 1 = single-ended
        │     │          └ 0 = 12-bit
        │     └ channel select
        └ start (must be 1)

 single-ended, 12-bit, ref+ADC on (PD=11):
   0x87 = TEMP0    0xA7 = VBAT    0xE7 = AUX    0xF7 = TEMP1
```

---

## 2. What does the stock firmware do?

The MikroTik `.npk` packages are a simple TLV container (magic `1e f1 d0 ba`) wrapping a standard **squashfs 4.0 / XZ** root filesystem. Unpacking it (`unsquashfs`) and grepping the kernel modules turns up `misc/voltage.ko`, which is **not stripped**:

```
$ mips-linux-gnu-objdump -t voltage.ko | grep ' F '
   ... init_rb450G_voltage, init_ath8316, sample_paired_data,
       rb400_spi_get, spi_async, register_voltage_device ...
```

So the monitor is an **SPI ADC** read by an `rb400_spi`-style driver — the ADS7846/ZT2046Q family. But `voltage.ko` only lists old RB4xx/RB7xx boards; the per-board wiring lives in the **kernel**, which is *not* in the `.npk` (the kernel is flashed separately).

## 3. Getting MikroTik's kernel

The kernel is embedded inside **netinstall**. Scanning the `netinstall` binary for ELF headers finds several `vmlinux` images; the big-endian MIPS one is the RouterOS-6 kernel (`Linux version 3.3.5`). Carved out and disassembled, it gives the platform code that `voltage.ko` calls into.

The board's flash dump confirms the identity from `hard_config` (mtd at `0xe000`):

```
RBMetalG-52SHPacn    "Metal 52 ac"    product code: g52c
```

`g52c` maps (in the kernel's board table) to platform **grebe** / machtype **0x4e (78)**, on the **scorpion** (QCA955x) SoC family.

## 4. The SPI architecture — chip-select is *native*, not a GPIO

Disassembling the kernel's `rb400_spi` transfer path shows it does **not** use a GPIO as the ADC chip-select. It puts the controller into user / bit-bang mode (`SPI_FS = 1`) and drives the chip-selects through the **`SPI_IOC` register** (`0x1f000000 + 0x08`):

```
   IOC bit 16 = CS0     IOC bit 17 = CS1     IOC bit 18 = CS2
   assert CSn:  IOC = 0x70000 & ~(0x10000 << n)
```

So the ADC sits on one of the controller's **native** chip-selects (CS1 or CS2), and those CS *outputs* are routed to physical pins by the GPIO output-function mux. That is the key insight: to find the ADC you do not chase a GPIO — you read the pinmux.

> A first attempt drove candidate GPIOs low looking for the CS line; one of them
> was a reset/watchdog line and rebooted the board. Don't brute-force GPIOs —
> the CS is a controller signal routed through the mux, read it instead.

## 5. Reading the live pinmux — finding the pin

On QCA953x/QCA955x the GPIO output-function registers hold one byte per pin = the function driven onto it (`8=SPI_CLK, 9=SPI_CS0, 10=SPI_CS1, 11=SPI_CS2, 12=SPI_MOSI`), and `GPIO_IN_ENABLE0` low byte selects the GPIO feeding the SPI data-in (MISO). The [`adc-diag`](tools/adc-diag/) kernel module dumps and decodes them on the running router:

```
adc-diag: GPIO5  output = func 0x09 = SPI_CS0     <- NOR flash
adc-diag: GPIO6  output = func 0x08 = SPI_CLK      } shared
adc-diag: GPIO7  output = func 0x0c = SPI_MOSI      } bus
adc-diag: GPIO17 output = func 0x0b = SPI_CS2     <- the ADC
adc-diag: SPI_DATA_IN (MISO) <- GPIO8              } shared
```

There it is:

* flash = CS0 (pin 5), CLK (pin 6), MOSI (pin 7), MISO (GPIO 8);
* **ADC = SPI_CS2 (pin 17)**;
* `SPI_CS1` appears on no pin — which is exactly why the device tree's original
  `reg = <1>` (CS1) toggled nothing and `/dev/spidev0.1` read `00 00 00`.

## 6. The device-tree fix

Put the ADC on the native CS2; the ath79 driver drives all three native chip-selects through `SPI_IOC`, so no `cs-gpios` are needed:

```dts
&spi {
        num-cs = <3>;                 /* was 2 — needed to reach CS2 */

        flash@0 { reg = <0>; ... };   /* CS0, NOR flash */

        adc@2 {                       /* was adc@1 / reg = <1> */
                compatible = "rohm,dh2228fv";   /* generic spidev */
                reg = <2>;            /* SPI_CS2 = pin 17 = ZT2046Q /CS */
                spi-max-frequency = <2000000>;
        };
};
```

The ADC now enumerates as **`/dev/spidev0.2`**.

A note on clock: the ath79 controller divides the 240 MHz AHB clock, so its minimum SPI clock is `240/128 ≈ 1.875 MHz`. Below that you get `spi clock is too low`. The ADS7846 family tops out near 2 MHz, so **2 MHz** is the one usable point.

## 7. The read protocol — two non-obvious rules

With the right CS, `/dev/spidev0.2` answered — but every channel returned the same drifting value. Two things were needed:

**(a) Hold `/CS` low for the whole multi-channel frame.** Issuing one `spi-pipe` call per channel toggles `/CS` between channels and resets the ADC's sample-and-hold; the readings collapse to a common value. Send the whole control-byte sequence in one transfer:

```
 one transfer, /CS low the whole time:
  ┌ throwaway ┐┌ TEMP0 ┐┌  AUX  ┐┌ TEMP1 ┐┌ flush ┐
  │ 87 00 00  ││87 00 00││E7 00 00││F7 00 00││00 00 00│   MOSI (control + read clocks)
  │  (junk)   ││ result ││ result ││ result ││        │   MISO result per group
  └───────────┘└────────┘└────────┘└────────┘└────────┘
   discard ▲      keep        keep      keep
```

**(b) Discard the first conversion of each frame.** The first sample after `/CS` goes low has not settled and reads garbage (it made the temperature jump to ~1300 °C every other read). So the frame opens with a throwaway conversion and the real channels are read from the groups after it. `rbmetal-health` averages a few such frames to smooth the ±1-count noise.

The 12-bit result of each channel is the top 12 bits of the two bytes that follow its control byte: `((hi << 8) | lo) >> 3 & 0xFFF`.

## 8. Calibration

The channel meanings and scaling were nailed down against known references.

**Voltage — channel AUX (`0xE7`).** Two bench supplies gave two points, which a linear fit turns into volts (a single through-zero scale left a ~0.2 V offset):

| supply | AUX raw |
|--------|---------|
| 12.0 V | 1240 |
| 24.0 V | 2438 |

```
 V = 12.0 + (raw - 1240) * (24.0 - 12.0) / (2438 - 1240)   [volts]
```

Reads 12.00 V and 24.00 V at the calibration points. (On-board divider ≈ 16:1, full scale ≈ 40 V against the ADC's 2.5 V internal reference.)

**Temperature — internal diode delta `TEMP1 − TEMP0`.** The ZT2046Q is a *clone*; its diode constants differ from TI's, so the datasheet formula is wrong — it reported ~62 °C at a real ~39 °C. But the delta is **proportional to absolute temperature (PTAT)**, so one reference point sets the scale:

```
 T(K) = (TEMP1 - TEMP0) * TEMP_CAL_TK / TEMP_CAL_DT
        with TEMP_CAL_DT = 213 counts at TEMP_CAL_TK = 312 K (39 °C)
```

This was then **validated at a second point**: cooling the board in a fridge to a ~25 °C case dropped the delta to ~203, which the same scale reports as ~26 °C (the ADC die runs a couple degrees above the case). The two points give the same `≈1.465 K/count` slope, confirming the PTAT model.

```
   delta (counts)        temperature
       213       ───────►   39 °C    (point 1)
       203       ───────►   25 °C    (point 2, fridge)
       slope ≈ 312/213 ≈ 298/203 ≈ 1.465 K per count  ✔ consistent
```

---

## 9. Result

```
$ rbmetal-health
voltage:     24.00 V
temperature: 38 C
```

Stable, and tracks reality: swapping the supply 24 V → 12 V reads 12.00 V, and the temperature climbs in real time as the board warms back up from the fridge.

## 10. Reproducing it / tools

* [`tools/adc-diag/`](tools/adc-diag/) — the read-only kernel module that dumps and decodes the SPI/GPIO pinmux (this is what found `pin17 = SPI_CS2`).
* [`../files/.../usr/bin/rbmetal-health`](../files/target/linux/ath79/mikrotik/base-files/usr/bin/rbmetal-health) — the production reader (calibration constants at the top).
* Manual one-off read from the shell:

  ```sh
  # AUX (voltage), one CS-low frame, discard the first (throwaway) conversion:
  printf '\347\0\0\347\0\0' | spi-pipe -d /dev/spidev0.2 -s 2000000 -b 6 -n 1 \
    | hexdump -v -e '6/1 "%02x "'
  # second group bytes 5,6 -> raw = ((b5<<8)|b6)>>3 & 0xFFF
  ```

## Key takeaways for other MikroTik ath79 boards

1. The voltage/temperature monitor is an **ADS7846/XPT2046-class SPI ADC** on the **boot SPI bus**, sharing CLK/MOSI/MISO with the flash; only its `/CS` differs.
2. The `/CS` is a **native controller chip-select** (CS1/CS2 via `SPI_IOC`), routed to a pin by the **GPIO output-function mux** — read the mux (`adc-diag`) instead of guessing GPIOs.
3. In the device tree it is just another `spidev` at the right `reg` (and `num-cs` raised to reach it); no `cs-gpios`.
4. Read all channels in **one `/CS`-low transfer** and **discard the first conversion**.
5. Calibrate voltage against a known supply and temperature against a known reading — the clone's temperature constants are not TI's.
