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
#include <linux/mtd/mtd.h>
#include <linux/mtd/partitions.h>
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

#define WA722M_GPIO_LED_WLAN_5G		3
#define WA722M_GPIO_LED_WLAN_2G		2
#define WA722M_GPIO_LED_STATUS		
#define WA722M_GPIO_LED_WPS		15

#define WA722M_GPIO_BTN_WPS		3
#define WA722M_GPIO_BTN_RESET	22

#define WA722M_KEYS_POLL_INTERVAL	20	/* msecs */
#define WA722M_KEYS_DEBOUNCE_INTERVAL	(3 * WA722M_KEYS_POLL_INTERVAL)

#define WA722M_MAC_OFFSET			0x8
#define WA722M_WMAC_CALDATA_OFFSET	0x1000
#define WA722M_PCIE_CALDATA_OFFSET	0x5000

static struct gpio_led wa722m_leds_gpio[] __initdata = {
	{
		.name		= "wa722m:green:wlan-2g",
		.gpio		= WA722M_GPIO_LED_WLAN_2G,
		.active_low	= 0,
	},
	{
		.name		= "wa722m:blue:wlan-5g",
		.gpio		= WA722M_GPIO_LED_WLAN_5G,
		.active_low	= 0,
	},
};

static struct gpio_keys_button wa722m_gpio_keys[] __initdata = {
	{
		.desc		= "reset button",
		.type		= EV_KEY,
		.code		= KEY_RESTART,
		.debounce_interval = WA722M_KEYS_DEBOUNCE_INTERVAL,
		.gpio		= WA722M_GPIO_BTN_RESET,
		.active_low	= 1,
	},
};

static struct ar8327_pad_cfg wa722m_ar8327_pad0_cfg = {
	.mode = AR8327_PAD_MAC_RGMII,
	.txclk_delay_en = true,
	.rxclk_delay_en = true,
	.txclk_delay_sel = AR8327_CLK_DELAY_SEL1,
	.rxclk_delay_sel = AR8327_CLK_DELAY_SEL2,
};

static struct ar8327_led_cfg wa722m_ar8327_led_cfg = {
	.led_ctrl0 = 0x00000000,
	.led_ctrl1 = 0xc737c737,
	.led_ctrl2 = 0x00000000,
	.led_ctrl3 = 0x00c30c00,
	.open_drain = true,
};

static struct ar8327_platform_data wa722m_ar8327_data = {
	.pad0_cfg = &wa722m_ar8327_pad0_cfg,
	.port0_cfg = {
		.force_link = 1,
		.speed = AR8327_PORT_SPEED_1000,
		.duplex = 1,
		.txpause = 1,
		.rxpause = 1,
	},
	.led_cfg = &wa722m_ar8327_led_cfg,
};

static struct mdio_board_info wa722m_mdio0_info[] = {
	{
		.bus_id = "ag71xx-mdio.0",
		.phy_addr = 0,
		.platform_data = &wa722m_ar8327_data,
	},
};

#define RTL8211E_PHY_ID 0x001cc915
#define AR8035_PHY_ID   0x004dd072
static void w722m_revise_config_ge0(unsigned int phy_id)
{
	switch (phy_id) {
	case RTL8211E_PHY_ID:
		ath79_eth0_pll_data.pll_10   = 0x00001313;
		ath79_eth0_pll_data.pll_100  = 0x00000101;
		ath79_eth0_pll_data.pll_1000 = 0x46000000;
		ath79_eth0_cfg_data.cfg_10   = 0x00000001;
		ath79_eth0_cfg_data.cfg_100  = 0x00000001;
		ath79_eth0_cfg_data.cfg_1000 = 0x00028001;
		break;
	case AR8035_PHY_ID  : 
		ath79_eth0_pll_data.pll_10   = 0x08001313;
		ath79_eth0_pll_data.pll_100  = 0x08000101;
		ath79_eth0_pll_data.pll_1000 = 0x0a000000;
		ath79_eth0_cfg_data.cfg_10   = 0x00000001;
		ath79_eth0_cfg_data.cfg_100  = 0x00000001;
		ath79_eth0_cfg_data.cfg_1000 = 0x00028001;
		break;
	default:
		break;
	}
}

static void __init wa722m_setup(void)
{
	u8 *mac = (u8 *) KSEG1ADDR(0x1ffe0000);
	u8 *art = (u8 *) KSEG1ADDR(0x1fff0000);
	u8 ethmac[ETH_ALEN];
	u8 tmpmac[ETH_ALEN];

	//ath79_register_m25p80(&wa722m_flash_data);
	ath79_register_m25p80(NULL);

	ath79_register_leds_gpio(-1, ARRAY_SIZE(wa722m_leds_gpio),
				 wa722m_leds_gpio);
	ath79_register_gpio_keys_polled(-1, WA722M_KEYS_POLL_INTERVAL,
					ARRAY_SIZE(wa722m_gpio_keys),
					wa722m_gpio_keys);
	
	ath79_parse_ascii_mac(mac + WA722M_MAC_OFFSET, ethmac);
	
	ath79_init_mac(tmpmac, ethmac, 0x1);
	ath79_register_wmac(art + WA722M_WMAC_CALDATA_OFFSET, tmpmac);
	
	ath79_init_mac(tmpmac, ethmac, 0x2);
	ap91_pci_init(art + WA722M_PCIE_CALDATA_OFFSET, tmpmac);

	ath79_setup_ar934x_eth_cfg(AR934X_ETH_CFG_RGMII_GMAC0 |
				   AR934X_ETH_CFG_SW_ONLY_MODE);

	/*ath79_register_mdio(1, 0x0);*/
	ath79_register_mdio(0, 0x0);

	ath79_init_mac(ath79_eth0_data.mac_addr, ethmac, 0);

	mdiobus_register_board_info(wa722m_mdio0_info,
				    ARRAY_SIZE(wa722m_mdio0_info));

	/* GMAC0 is connected to an AR8327 switch */
	ath79_eth0_data.phy_if_mode = PHY_INTERFACE_MODE_RGMII;
	ath79_eth0_data.phy_mask = BIT(0);
	ath79_eth0_data.mii_bus_dev = &ath79_mdio0_device.dev;
	ath79_eth0_pll_data.pll_1000 = 0x06000000;
	ath79_eth0_data.revise_config = w722m_revise_config_ge0;
	ath79_register_eth(0);
	
	ath79_register_nfc();
}

MIPS_MACHINE(ATH79_MACH_WA722M_E, "WA722M-E", "DUNCHONG WA722M-E",
	     wa722m_setup);
