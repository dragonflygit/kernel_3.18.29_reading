/*
 * Atheros AP147 reference board support
 *
 * Copyright (c) 2013 The Linux Foundation. All rights reserved.
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

#include <linux/gpio.h>
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
#include "dev-spi.h"
#include "dev-usb.h"
#include "dev-wmac.h"
#include "machtypes.h"

#define AP161_GPIO_LED_WAN			15
//#define AP161_GPIO_LED_LAN			11
#define AP161_GPIO_LED_WLAN_2G		16
#define AP161_GPIO_LED_WLAN_5G		12
//#define AP161_GPIO_LED_STATUS		13
#define AP161_GPIO_BTN_RST			17
#define AP161_KEYS_POLL_INTERVAL	20	/* msecs */
#define AP161_KEYS_DEBOUNCE_INTERVAL	(3 * AP161_KEYS_POLL_INTERVAL)

#define AP161_MAC0_OFFSET		0
#define AP161_MAC1_OFFSET		6
#define AP161_WMAC_CALDATA_OFFSET	0x1000
#define AP161_PCIE_CALDATA_OFFSET	0x5000

static struct gpio_led ap161_leds_gpio[] __initdata = {
	{
		.name		= "ap161:wlan-2g",
		.gpio		= AP161_GPIO_LED_WLAN_2G,
		.active_low	= 1,
		.default_state  = LEDS_GPIO_DEFSTATE_ON,
	},
	{
		.name		= "ap161:wlan-5g",
		.gpio		= AP161_GPIO_LED_WLAN_5G,
		.active_low	= 1,
	},
	{
		.name		= "ap161:wan",
		.gpio		= AP161_GPIO_LED_WAN,
		.active_low	= 1,
	},
};

static struct gpio_keys_button ap161_gpio_keys[] __initdata = {
	{
		.desc		= "RESET button",
		.type		= EV_KEY,
		.code		= KEY_RESTART,
		.debounce_interval = AP161_KEYS_DEBOUNCE_INTERVAL,
		.gpio		= AP161_GPIO_BTN_RST,
		.active_low	= 1,
	},
};

static void __init ap161_setup(void)
{
	u8 *art = (u8 *) KSEG1ADDR(0x1fff0000);

	ath79_register_m25p80(NULL);

	ath79_register_leds_gpio(-1, ARRAY_SIZE(ap161_leds_gpio),
				 ap161_leds_gpio);
	ath79_register_gpio_keys_polled(-1, AP161_KEYS_POLL_INTERVAL,
					ARRAY_SIZE(ap161_gpio_keys),
					ap161_gpio_keys);

	ath79_register_wmac(art + AP161_WMAC_CALDATA_OFFSET, NULL);
	
	ath79_register_mdio(0, 0x0);
	ath79_register_mdio(1, 0x0);

	/* GMAC0 is connected to PHY4 of the internal switch */
	ath79_init_mac(ath79_eth0_data.mac_addr, art + AP161_MAC0_OFFSET, 0);
	ath79_eth0_data.phy_if_mode = PHY_INTERFACE_MODE_MII;
	ath79_eth1_data.speed = SPEED_100;
	ath79_eth1_data.duplex = DUPLEX_FULL;
	ath79_eth0_data.phy_mask = BIT(4);
	ath79_register_eth(0);

	/* GMAC1 is connected to the internal switch */
	ath79_init_mac(ath79_eth1_data.mac_addr, art + AP161_MAC1_OFFSET, 0);
	ath79_eth1_data.phy_if_mode = PHY_INTERFACE_MODE_GMII;
	ath79_eth1_data.speed = SPEED_1000;
	ath79_eth1_data.duplex = DUPLEX_FULL;
	ath79_switch_data.phy_poll_mask |= BIT(4);
	ath79_switch_data.phy4_mii_en = 1;
	ath79_register_eth(1);

	ap91_pci_init(art + 0x5000, NULL);
}

MIPS_MACHINE(ATH79_MACH_RT_AP161, "RT-AP161", "RippleTek AP161",
	     ap161_setup);
MIPS_MACHINE(ATH79_MACH_FEEAP_X4S, "FEEAP-X4S", "FEEAP X4S",
	     ap161_setup);
