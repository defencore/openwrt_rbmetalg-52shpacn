// SPDX-License-Identifier: GPL-2.0
/*
 * adc-diag - SPI controller + GPIO pinmux dump for QCA955x MikroTik boards.
 *
 * This is the diagnostic that located the ZT2046Q ADC's chip-select on the
 * RouterBOARD Metal 52 ac. The ath79 boot-SPI controller drives its three
 * chip-selects (CS0/CS1/CS2) and clock/data through the SoC's GPIO output-
 * function mux, so the wiring can be read straight out of the live registers:
 *
 *   - GPIO_OUT_FUNCTION0..5 (0x2c..0x40): one byte per pin = its output
 *     function. On QCA953x/QCA955x the SPI function codes are
 *     8=SPI_CLK, 9=SPI_CS0, 10=SPI_CS1, 11=SPI_CS2, 12=SPI_MOSI.
 *   - GPIO_IN_ENABLE0 (0x44) low byte = the GPIO that feeds SPI_DATA_IN (MISO).
 *
 * On this board the dump shows pin5=CS0, pin6=CLK, pin7=MOSI, MISO<-GPIO8 and
 * **pin17=SPI_CS2**, while SPI_CS1 is muxed to no pin -- i.e. the ADC is on
 * native chip-select 2, which is why the device tree must use reg = <2>.
 *
 * Load it, read dmesg, then it unloads itself (init returns an error on
 * purpose). All registers are read-only; nothing is changed.
 *
 * Build against the matching OpenWrt kernel tree -- see README.md.
 */
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/io.h>

#define SPI_BASE	0x1f000000u
#define GPIO_BASE	0x18040000u

#define GPIO_OE		0x00
#define GPIO_IN		0x04
#define GPIO_OUT	0x08
#define GPIO_OUT_FUNC0	0x2c	/* .. 0x40, one byte per pin */
#define GPIO_IN_EN0	0x44	/* low byte = SPI_DATA_IN source GPIO */

static const char *spi_out_func(u8 f)
{
	switch (f) {
	case 8:  return "SPI_CLK";
	case 9:  return "SPI_CS0";
	case 10: return "SPI_CS1";
	case 11: return "SPI_CS2";
	case 12: return "SPI_MOSI";
	default: return NULL;
	}
}

static int __init adc_diag_init(void)
{
	void __iomem *spi, *gpio;
	int reg, b, pin;

	spi  = ioremap(SPI_BASE, 0x10);
	gpio = ioremap(GPIO_BASE, 0x100);
	if (!spi || !gpio) {
		if (spi)
			iounmap(spi);
		if (gpio)
			iounmap(gpio);
		return -ENOMEM;
	}

	pr_info("adc-diag: SPI  FS=%08x CTRL=%08x IOC=%08x RDS=%08x\n",
		__raw_readl(spi + 0x00), __raw_readl(spi + 0x04),
		__raw_readl(spi + 0x08), __raw_readl(spi + 0x0c));
	pr_info("adc-diag: GPIO OE=%08x IN=%08x OUT=%08x IN_ENABLE0=%08x\n",
		__raw_readl(gpio + GPIO_OE), __raw_readl(gpio + GPIO_IN),
		__raw_readl(gpio + GPIO_OUT), __raw_readl(gpio + GPIO_IN_EN0));

	/* Decode the output-function mux: which pin carries which SPI signal. */
	for (reg = 0; reg < 6; reg++) {
		u32 v = __raw_readl(gpio + GPIO_OUT_FUNC0 + reg * 4);

		for (b = 0; b < 4; b++) {
			u8 f = (v >> (b * 8)) & 0xff;
			const char *nm = spi_out_func(f);

			if (nm) {
				pin = reg * 4 + b;
				pr_info("adc-diag: GPIO%-2d output = func 0x%02x = %s\n",
					pin, f, nm);
			}
		}
	}
	pr_info("adc-diag: SPI_DATA_IN (MISO) <- GPIO%d\n",
		__raw_readl(gpio + GPIO_IN_EN0) & 0xff);

	iounmap(spi);
	iounmap(gpio);
	return -EINVAL;		/* one-shot: do not stay resident */
}

module_init(adc_diag_init);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("QCA955x SPI/GPIO pinmux dump (finds the ZT2046Q chip-select)");
