/*
 * Atheros DB120 reference board support
 *
 * Copyright (c) 2011 Qualcomm Atheros
 * Copyright (c) 2011-2012 Gabor Juhos <juhosg@openwrt.org>
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 *
 */

#include <linux/pci.h>
#include <linux/phy.h>
#include <linux/platform_device.h>
#include <linux/ath9k_platform.h>
#include <linux/ar8216_platform.h>

#include <asm/mach-ath79/ar71xx_regs.h>

#include "common.h"
#include "dev-ap9x-pci.h"
#include "dev-eth.h"
#include "dev-gpio-buttons.h"
#include "dev-leds-gpio.h"
#include "dev-m25p80.h"
#include "dev-nfc.h"
#include "dev-spi.h"
#include "dev-usb.h"
#include "dev-wmac.h"
#include "machtypes.h"


#define ROICX_GPIO_LED_WLAN			13
#define ROICX_GPIO_LED_SYS			14
#define ROICX_GPIO_LED_LAN1			19
#define ROICX_GPIO_LED_LAN2			20
#define ROICX_GPIO_LED_LAN3			21
#define ROICX_GPIO_LED_LAN4			22
#define ROICX_GPIO_LED_WAN			18
#define ROICX_GPIO_BTN_RESET		17
#define ROICX_GPIO_LED_USB			15

#define ROICX_KEYS_POLL_INTERVAL	20	/* msecs */
#define ROICX_KEYS_DEBOUNCE_INTERVAL	(3 * ROICX_KEYS_POLL_INTERVAL)

#define ROICX_MAC0_OFFSET		0
#define ROICX_MAC1_OFFSET		6
#define ROICX_WMAC_CALDATA_OFFSET	0x1000
#define ROICX_PCIE_CALDATA_OFFSET	0x5000

#define CONFIG_ENCRY_MAC_LENGTH_IN_FLASH	18
#define ETH_ALEN 6

static struct gpio_led roicx_q2s_leds_gpio[] __initdata = {
	{
		.name		= "roicx-q2s:green:status",
		.gpio		= ROICX_GPIO_LED_SYS,
		.active_low	= 0,
	},
	{
		.name		= "roicx-q2s:blue:wlan-2g",
		.gpio		= ROICX_GPIO_LED_WLAN,
		.active_low	= 0,
	},
	{
		.name		= "roicx-q2s:green:lan",
		.gpio		= ROICX_GPIO_LED_LAN4,
		.active_low	= 1,
	},
	{
		.name		= "roicx-q2s:green:wan",
		.gpio		= ROICX_GPIO_LED_WAN,
		.active_low	= 1,
	},
};

static struct gpio_keys_button roicx_q2s_gpio_keys[] __initdata = {
	{
        .desc        = "reset",
        .type        = EV_KEY,
        .code        = KEY_RESTART,
        .debounce_interval = ROICX_KEYS_DEBOUNCE_INTERVAL,
        .gpio        = ROICX_GPIO_BTN_RESET,
        .active_low    = 1,
    },
};
static struct gpio_led roicx_f2s_leds_gpio[] __initdata = {
	{
		.name		= "roicx-f2s:green:status",
		.gpio		= ROICX_GPIO_LED_SYS,
		.active_low	= 1,
	},
	{
		.name		= "roicx-f2s:green:wlan-2g",
		.gpio		= ROICX_GPIO_LED_WLAN,
		.active_low	= 1,
	},
	{
		.name		= "roicx-f2s:green:wan",
		.gpio		= ROICX_GPIO_LED_WAN,
		.active_low	= 1,
	},
	{
		.name		= "roicx-f2s:green:lan1",
		.gpio		= ROICX_GPIO_LED_LAN1,
		.active_low	= 1,
	},
	{
		.name		= "roicx-f2s:green:lan2",
		.gpio		= ROICX_GPIO_LED_LAN2,
		.active_low	= 1,
	},
	{
		.name		= "roicx-f2s:green:lan3",
		.gpio		= ROICX_GPIO_LED_LAN3,
		.active_low	= 1,
	},
	{
		.name		= "roicx-f2s:green:lan4",
		.gpio		= ROICX_GPIO_LED_LAN4,
		.active_low	= 1,
	},
	{
		.name		= "roicx-f2s:green:usb",
		.gpio		= ROICX_GPIO_LED_USB,
		.active_low	= 1,
	}
};

static struct gpio_keys_button roicx_f2s_gpio_keys[] __initdata = {
	{
        .desc        = "reset",
        .type        = EV_KEY,
        .code        = KEY_RESTART,
        .debounce_interval = ROICX_KEYS_DEBOUNCE_INTERVAL,
        .gpio        = ROICX_GPIO_BTN_RESET,
        .active_low    = 1,
    },
};

static void __init roicx_gmac_setup(void)
{
	void __iomem *base;
	u32 t;

	base = ioremap(AR934X_GMAC_BASE, AR934X_GMAC_SIZE);

	t = __raw_readl(base + AR934X_GMAC_REG_ETH_CFG);
	t &= ~(AR934X_ETH_CFG_RGMII_GMAC0 | AR934X_ETH_CFG_MII_GMAC0 |
	       AR934X_ETH_CFG_GMII_GMAC0 | AR934X_ETH_CFG_SW_ONLY_MODE | AR934X_ETH_CFG_SW_PHY_SWAP);

	__raw_writel(t, base + AR934X_GMAC_REG_ETH_CFG);

	__raw_readl(base + AR934X_GMAC_REG_ETH_CFG);

	iounmap(base);
}

void mac_plus_one(u8 *src, u8 *dst)
{
    s32 i;

    memcpy(dst, src, 6);
    for(i = 5; i >=0; i--)
    {
        if(dst[i] != 0xff)
        {
            dst[i]++;
            break;
        }
        else
        {   
            dst[i] = 0x00;
        }   
    }   
}

static void __init roicx_q2s_setup(void)
{
	u8 *art = (u8 *) KSEG1ADDR(0x1fff0000);
	u8 *mac = (u8 *) KSEG1ADDR(0x1ffe0000);
	
	ath79_register_m25p80(NULL);

	ath79_register_leds_gpio(-1, ARRAY_SIZE(roicx_q2s_leds_gpio),
				 roicx_q2s_leds_gpio);
	ath79_register_gpio_keys_polled(-1, ROICX_KEYS_POLL_INTERVAL,
					ARRAY_SIZE(roicx_q2s_gpio_keys),
					roicx_q2s_gpio_keys);

	ath79_register_wmac(art + ROICX_WMAC_CALDATA_OFFSET, mac);

	roicx_gmac_setup();

	ath79_register_mdio(1, 0x0);
	
	/* GMAC1 is connected to the internal switch */
	ath79_init_mac(ath79_eth1_data.mac_addr, mac, 1);
	ath79_eth1_data.phy_if_mode = PHY_INTERFACE_MODE_GMII;
	ath79_register_eth(1);

	/* GMAC0 is connected to PHY4 of the internal switch */
	ath79_init_mac(ath79_eth0_data.mac_addr, mac, 2);
	ath79_switch_data.phy4_mii_en = 1;
	ath79_switch_data.phy_poll_mask = BIT(4);
	ath79_eth0_data.phy_if_mode = PHY_INTERFACE_MODE_MII;
	ath79_eth0_data.phy_mask = BIT(4);
	ath79_eth0_data.mii_bus_dev = &ath79_mdio1_device.dev;
	
	ath79_register_eth(0);
}

MIPS_MACHINE(ATH79_MACH_ROICX_Q2S, "ROICX-Q2S", "ROICX Q2S",
	     roicx_q2s_setup);

static void __init roicx_f2s_setup(void)
{
	u8 *art = (u8 *) KSEG1ADDR(0x1fff0000);
	u8 *mac = (u8 *) KSEG1ADDR(0x1ffe0000);

	ath79_register_m25p80(NULL);

	ath79_gpio_output_select(ROICX_GPIO_LED_USB, AR934X_GPIO_OUT_GPIO);
	ath79_register_leds_gpio(-1, ARRAY_SIZE(roicx_f2s_leds_gpio),
				 roicx_f2s_leds_gpio);
	ath79_register_gpio_keys_polled(-1, ROICX_KEYS_POLL_INTERVAL,
					ARRAY_SIZE(roicx_f2s_gpio_keys),
					roicx_f2s_gpio_keys);

	ath79_register_wmac(art + ROICX_WMAC_CALDATA_OFFSET, mac);

	roicx_gmac_setup();

	ath79_register_mdio(1, 0x0);
	
	/* GMAC1 is connected to the internal switch */
	ath79_init_mac(ath79_eth1_data.mac_addr, mac, 1);
	ath79_eth1_data.phy_if_mode = PHY_INTERFACE_MODE_GMII;
	ath79_register_eth(1);

	/* GMAC0 is connected to PHY4 of the internal switch */
	ath79_init_mac(ath79_eth0_data.mac_addr, mac, 2);
	ath79_switch_data.phy4_mii_en = 1;
	ath79_switch_data.phy_poll_mask = BIT(4);
	ath79_eth0_data.phy_if_mode = PHY_INTERFACE_MODE_MII;
	ath79_eth0_data.phy_mask = BIT(4);
	ath79_eth0_data.mii_bus_dev = &ath79_mdio1_device.dev;
	
	ath79_register_eth(0);

}
MIPS_MACHINE(ATH79_MACH_ROICX_F2S, "ROICX-F2S", "ROICX F2S",
	     roicx_f2s_setup);
