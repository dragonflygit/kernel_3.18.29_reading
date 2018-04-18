/*
 * P&W AP501N reference board support
 *
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

#define ETH_MAC0_OFFSET		0
#define ETH_MAC1_OFFSET		6
#define WMAC_CALDATA_OFFSET 0x1000
#define PCIE_CALDATA_OFFSET 0x5000

#define KEYS_POLL_INTERVAL  20  /* msecs */
#define KEYS_DEBOUNCE_INTERVAL  (3 * KEYS_POLL_INTERVAL)

#define AP501N_GPIO_LED_USB		11
#define AP501N_GPIO_LED_WLAN_5G		6
#define AP501N_GPIO_LED_WLAN_2G		20
#define AP501N_GPIO_LED_STATUS		21
#define AP301N_GPIO_LED_BLUE		12
#define AP301N_GPIO_LED_RED		21
#define AP301N_GPIO_LED_GREEN		22
#define AP301N_SELECT_EX_ANTENNA_OFF	(0x0120)
#define AP301N_GPIO_SELECT_ANTENNA_BIT1	(13)
#define AP301N_GPIO_SELECT_ANTENNA_BIT0	(14)
#define AP301N_SELECT_EX_ANTENNA		(0x1)

#define AP501N_GPIO_BTN_RST		3
#define AP301N_GPIO_BTN_RST		20

#define R612N_GPIO_LED_WLAN		13
#define R612N_GPIO_LED_WAN		18
#define R612N_GPIO_LED_LAN1		19
#define R612N_GPIO_LED_LAN2		20
#define R612N_GPIO_LED_LAN3		21
#define R612N_GPIO_LED_LAN4		22

#define R612N_GPIO_BTN_RST		17

static struct gpio_led ap501n_leds_gpio[] __initdata = {
	{
		.name		= "ap501n:green:status",
		.gpio		= AP501N_GPIO_LED_STATUS,
		.active_low	= 1,
	},
	{
		.name		= "ap501n:green:wlan-2g",
		.gpio		= AP501N_GPIO_LED_WLAN_2G,
		.active_low	= 1,
	},
};

static struct gpio_keys_button ap501n_gpio_keys[] __initdata = {
	{
		.desc		= "RESET button",
		.type		= EV_KEY,
		.code		= KEY_RESTART,
		.debounce_interval = KEYS_DEBOUNCE_INTERVAL,
		.gpio		= AP501N_GPIO_BTN_RST,
	},
};

static struct ar8327_pad_cfg ap501n_ar8327_pad0_cfg = {
	.mode = AR8327_PAD_MAC_RGMII,
	.txclk_delay_en = true,
	.rxclk_delay_en = true,
	.txclk_delay_sel = AR8327_CLK_DELAY_SEL1,
	.rxclk_delay_sel = AR8327_CLK_DELAY_SEL2,
};

static struct ar8327_led_cfg ap501n_ar8327_led_cfg = {
	.led_ctrl0 = 0x00000000,
	.led_ctrl1 = 0xc737c737,
	.led_ctrl2 = 0x00000000,
	.led_ctrl3 = 0x00c30c00,
	.open_drain = true,
};

static struct ar8327_platform_data ap501n_ar8327_data = {
	.pad0_cfg = &ap501n_ar8327_pad0_cfg,
	.port0_cfg = {
		.force_link = 1,
		.speed = AR8327_PORT_SPEED_1000,
		.duplex = 1,
		.txpause = 1,
		.rxpause = 1,
	},
	.led_cfg = &ap501n_ar8327_led_cfg,
};

static struct mdio_board_info ap501n_mdio0_info[] = {
	{
		.bus_id = "ag71xx-mdio.0",
		.phy_addr = 0,
		.platform_data = &ap501n_ar8327_data,
	},
};

static struct gpio_led r612n_leds_gpio[] __initdata = {
	{
		.name		= "r612n:green:wlan",
		.gpio		= R612N_GPIO_LED_WLAN,
		.active_low	= 1,
	},
	{
		.name		= "r612n:green:wan",
		.gpio		= R612N_GPIO_LED_WAN,
		.active_low	= 1,
	},
	{
		.name		= "r612n:green:lan1",
		.gpio		= R612N_GPIO_LED_LAN1,
		.active_low	= 1,
	},
	{
		.name		= "r612n:green:lan2",
		.gpio		= R612N_GPIO_LED_LAN2,
		.active_low	= 1,
	},
	{
		.name		= "r612n:green:lan3",
		.gpio		= R612N_GPIO_LED_LAN3,
		.active_low	= 1,
	},
	{
		.name		= "r612n:green:lan4",
		.gpio		= R612N_GPIO_LED_LAN4,
		.active_low	= 1,
	},
};

static struct gpio_keys_button r612n_gpio_keys[] __initdata = {
	{
		.desc		= "RESET button",
		.type		= EV_KEY,
		.code		= KEY_RESTART,
		.debounce_interval = KEYS_DEBOUNCE_INTERVAL,
		.gpio		= R612N_GPIO_BTN_RST,
	},
};

static void __init ap501n_setup(void)
{
	u8 *art = (u8 *) KSEG1ADDR(0x1f050000);

	ath79_gpio_output_select(AP501N_GPIO_LED_USB, AR934X_GPIO_OUT_GPIO);
	ath79_register_m25p80(NULL);

	ath79_register_leds_gpio(-1, ARRAY_SIZE(ap501n_leds_gpio),
				 ap501n_leds_gpio);
	ath79_register_gpio_keys_polled(-1, KEYS_POLL_INTERVAL,
					ARRAY_SIZE(ap501n_gpio_keys),
					ap501n_gpio_keys);
	ath79_register_usb();
	ath79_register_wmac(art + WMAC_CALDATA_OFFSET, NULL);
	ap9x_pci_setup_wmac_led_pin(0, 6);
	ap91_pci_init(art + PCIE_CALDATA_OFFSET, NULL);

	ath79_setup_ar934x_eth_cfg(AR934X_ETH_CFG_RGMII_GMAC0 |
				   AR934X_ETH_CFG_SW_ONLY_MODE);

	ath79_register_mdio(1, 0x0);
	ath79_register_mdio(0, 0x0);

	ath79_init_mac(ath79_eth0_data.mac_addr, art + ETH_MAC0_OFFSET, 0);

	mdiobus_register_board_info(ap501n_mdio0_info,
				    ARRAY_SIZE(ap501n_mdio0_info));

	/* GMAC0 is connected to an AR8327 switch */
	ath79_eth0_data.phy_if_mode = PHY_INTERFACE_MODE_RGMII;
	ath79_eth0_data.phy_mask = BIT(0);
	ath79_eth0_data.mii_bus_dev = &ath79_mdio0_device.dev;
	ath79_eth0_pll_data.pll_1000 = 0x06000000;
	ath79_register_eth(0);

	/* GMAC1 is connected to the internal switch */
	ath79_init_mac(ath79_eth1_data.mac_addr, art + ETH_MAC1_OFFSET, 0);
	ath79_eth1_data.phy_if_mode = PHY_INTERFACE_MODE_GMII;
	ath79_eth1_data.speed = SPEED_1000;
	ath79_eth1_data.duplex = DUPLEX_FULL;

	ath79_register_eth(1);
}

MIPS_MACHINE(ATH79_MACH_PW_AP501N, "PW-AP501N", "P&W AP501N",
	     ap501n_setup);

static struct gpio_led ap301n_leds_gpio[] __initdata = {
	{
		.name		= "ap301n:blue",
		.gpio		= AP301N_GPIO_LED_BLUE,
		.active_low	= 0,
	}, {
		.name		= "ap301n:red",
		.gpio		= AP301N_GPIO_LED_RED,
		.active_low	= 0,
		.default_state  = LEDS_GPIO_DEFSTATE_ON,
	}, {
		.name		= "ap301n:green",
		.gpio		= AP301N_GPIO_LED_GREEN,
		.active_low	= 0,
	},
};

static struct gpio_keys_button ap301n_gpio_keys[] __initdata = {
	{
		.desc		= "Reset button",
		.type		= EV_KEY,
		.code		= KEY_RESTART,
		.debounce_interval = KEYS_DEBOUNCE_INTERVAL,
		.gpio		= AP301N_GPIO_BTN_RST,
	}
};

static void __init ap301n_setup(void)
{
	u8 *mac = (u8 *) KSEG1ADDR(0x1f7f0000);
	u8 *ee = (u8 *) KSEG1ADDR(0x1f7f1000);
	u8 enExanteFlag=*(mac+AP301N_SELECT_EX_ANTENNA_OFF);

	ath79_register_m25p80(NULL);


	ath79_register_leds_gpio(-1, ARRAY_SIZE(ap301n_leds_gpio),
				 ap301n_leds_gpio);

	ath79_register_gpio_keys_polled(1, KEYS_POLL_INTERVAL,
					ARRAY_SIZE(ap301n_gpio_keys),
					ap301n_gpio_keys);

	/* for select antenna: external-0:1 internal-1:0(default) */
	ath79_gpio_output_select(AP301N_GPIO_SELECT_ANTENNA_BIT1, 0);
	ath79_gpio_output_select(AP301N_GPIO_SELECT_ANTENNA_BIT0, 0);
	if (enExanteFlag == AP301N_SELECT_EX_ANTENNA)
	{	/* external antenna */
		printk("\nAP301N:select external antenna;enExanteFlag=0x%x",enExanteFlag);
		gpio_set_value(AP301N_GPIO_SELECT_ANTENNA_BIT1, 0);
		gpio_set_value(AP301N_GPIO_SELECT_ANTENNA_BIT0, 1);
	}
	else	/* internal antenna */
	{
		gpio_set_value(AP301N_GPIO_SELECT_ANTENNA_BIT1, 1);
		gpio_set_value(AP301N_GPIO_SELECT_ANTENNA_BIT0, 0);
	}

	//ath79_setup_ar934x_eth_cfg(AR934X_ETH_CFG_SW_PHY_SWAP);

	ath79_register_mdio(1, 0x0);

	ath79_init_mac(ath79_eth0_data.mac_addr, mac+ETH_MAC0_OFFSET, 0);
	ath79_init_mac(ath79_eth1_data.mac_addr, mac+ETH_MAC1_OFFSET, 0);

	/* GMAC0 is connected to the PHY0 of the internal switch */
	ath79_switch_data.phy4_mii_en = 1;
	ath79_switch_data.phy_poll_mask = BIT(4);
	ath79_eth0_data.phy_if_mode = PHY_INTERFACE_MODE_MII;
	ath79_eth0_data.phy_mask = BIT(4);
	ath79_eth0_data.mii_bus_dev = &ath79_mdio1_device.dev;
	ath79_register_eth(0);

	/* GMAC1 is connected to the internal switch */
	ath79_eth1_data.phy_if_mode = PHY_INTERFACE_MODE_GMII;
	ath79_register_eth(1);

	ath79_register_wmac(ee, NULL);
}

MIPS_MACHINE(ATH79_MACH_PW_AP301N, "PW-AP301N", "P&W AP301N",
	     ap301n_setup);

static void __init r612n_setup(void)
{
	u8 *mac = (u8 *) KSEG1ADDR(0x1f7f0000);
	u8 *ee = (u8 *) KSEG1ADDR(0x1f7f1000);

	ath79_register_m25p80(NULL);


	ath79_register_leds_gpio(-1, ARRAY_SIZE(r612n_leds_gpio),
				 r612n_leds_gpio);

	ath79_register_gpio_keys_polled(1, KEYS_POLL_INTERVAL,
					ARRAY_SIZE(r612n_gpio_keys),
					r612n_gpio_keys);

	ath79_register_mdio(1, 0x0);

	ath79_init_mac(ath79_eth0_data.mac_addr, mac+ETH_MAC0_OFFSET, 0);
	ath79_init_mac(ath79_eth1_data.mac_addr, mac+ETH_MAC1_OFFSET, 0);

	/* GMAC0 is connected to the PHY0 of the internal switch */
	ath79_switch_data.phy4_mii_en = 1;
	ath79_switch_data.phy_poll_mask = BIT(4);
	ath79_eth0_data.phy_if_mode = PHY_INTERFACE_MODE_MII;
	ath79_eth0_data.phy_mask = BIT(4);
	ath79_eth0_data.mii_bus_dev = &ath79_mdio1_device.dev;
	ath79_register_eth(0);

	/* GMAC1 is connected to the internal switch */
	ath79_eth1_data.phy_if_mode = PHY_INTERFACE_MODE_GMII;
	ath79_register_eth(1);

	ath79_register_wmac(ee, NULL);
}

MIPS_MACHINE(ATH79_MACH_PW_R612N, "PW-R612N", "P&W R612N",
	     r612n_setup);

#define AP506AN_GPIO_LED_WLAN_5G	14
#define AP506AN_GPIO_LED_WLAN_2G	15
#define AP506AN_GPIO_LED_STATUS		13
#define AP506AN_GPIO_BTN_RST		16

static struct gpio_led ap506an_leds_gpio[] __initdata = {
	{
		.name		= "ap506an:red:status",
		.gpio		= AP506AN_GPIO_LED_STATUS,
		.active_low	= 1,
	},
	{
		.name		= "ap506an:blue:wlan-2g",
		.gpio		= AP506AN_GPIO_LED_WLAN_2G,
		.active_low	= 1,
	},
	{
		.name		= "ap506an:green:wlan-5g",
		.gpio		= AP506AN_GPIO_LED_WLAN_5G,
		.active_low	= 1,
	},
};

static struct gpio_keys_button ap506an_gpio_keys[] __initdata = {
	{
		.desc		= "RESET button",
		.type		= EV_KEY,
		.code		= KEY_RESTART,
		.debounce_interval = KEYS_DEBOUNCE_INTERVAL,
		.gpio		= AP506AN_GPIO_BTN_RST,
		.active_low	= 1,
	},
};

static void __init ap506an_setup(void)
{
	u8 *art = (u8 *) KSEG1ADDR(0x1f050000);

	ath79_register_m25p80(NULL);

	ath79_register_leds_gpio(-1, ARRAY_SIZE(ap506an_leds_gpio),
				 ap506an_leds_gpio);
	ath79_register_gpio_keys_polled(-1, KEYS_POLL_INTERVAL,
					ARRAY_SIZE(ap506an_gpio_keys),
					ap506an_gpio_keys);

	ath79_register_wmac(art + WMAC_CALDATA_OFFSET, NULL);
	
	ap9x_pci_setup_wmac_led_pin(0, 6);
	ap91_pci_init(art + PCIE_CALDATA_OFFSET, NULL);

	ath79_setup_ar934x_eth_cfg(0x3c001);

	ath79_register_mdio(1, 0x0);
	ath79_register_mdio(0, 0x0);

	/* GMAC0 is connected to an RTL8211E phy */
	ath79_init_mac(ath79_eth0_data.mac_addr, art + ETH_MAC0_OFFSET, 0);
	ath79_eth0_data.phy_if_mode = PHY_INTERFACE_MODE_RGMII;
	ath79_eth0_data.phy_mask = BIT(0);
	ath79_eth0_data.mii_bus_dev = &ath79_mdio0_device.dev;
	ath79_register_eth(0);

	/* GMAC1 is connected to the internal switch */
	ath79_init_mac(ath79_eth1_data.mac_addr, art + ETH_MAC1_OFFSET, 0);
	ath79_eth1_data.phy_if_mode = PHY_INTERFACE_MODE_GMII;
	ath79_eth1_data.speed = SPEED_1000;
	ath79_eth1_data.duplex = DUPLEX_FULL;
	ath79_register_eth(1);
}

MIPS_MACHINE(ATH79_MACH_PW_AP506AN, "PW-AP506AN", "P&W AP506AN",
	     ap506an_setup);

#define AP603AC_GPIO_LED_WLAN_5G	4
#define AP603AC_GPIO_LED_WLAN_2G	12
#define AP603AC_GPIO_LED_STATUS		14
#define AP603AC_GPIO_BTN_RST		13
static struct gpio_led ap603ac_leds_gpio[] __initdata = {
	{
		.name		= "pw:red:status",
		.gpio		= AP603AC_GPIO_LED_STATUS,
		.active_low	= 0,
	},
	{
		.name		= "pw:blue:wlan-2g",
		.gpio		= AP603AC_GPIO_LED_WLAN_2G,
		.active_low	= 0,
	},
	{
		.name		= "pw:green:wlan-5g",
		.gpio		= AP603AC_GPIO_LED_WLAN_5G,
		.active_low	= 0,
	},
};

static struct gpio_keys_button ap603ac_gpio_keys[] __initdata = {
	{
		.desc		= "RESET button",
		.type		= EV_KEY,
		.code		= KEY_RESTART,
		.debounce_interval = KEYS_DEBOUNCE_INTERVAL,
		.gpio		= AP603AC_GPIO_BTN_RST,
		.active_low	= 1,
	},
};

static void __init ap603ac_setup(void)
{
	u8 *art = (u8 *) KSEG1ADDR(0x1fff0000);

	ath79_register_m25p80(NULL);

	ath79_register_leds_gpio(-1, ARRAY_SIZE(ap603ac_leds_gpio),
				 ap603ac_leds_gpio);
	ath79_register_gpio_keys_polled(-1, KEYS_POLL_INTERVAL,
					ARRAY_SIZE(ap603ac_gpio_keys),
					ap603ac_gpio_keys);

	ath79_register_wmac(art + WMAC_CALDATA_OFFSET, NULL);

	ap9x_pci_setup_wmac_led_pin(0, AP603AC_GPIO_LED_WLAN_5G);
	ap91_pci_init(art + PCIE_CALDATA_OFFSET, NULL);

	ath79_register_mdio(0, 0x0);
	ath79_register_mdio(1, 0x0);

	ath79_init_mac(ath79_eth0_data.mac_addr, art + ETH_MAC0_OFFSET, 0);
	ath79_init_mac(ath79_eth1_data.mac_addr, art + ETH_MAC1_OFFSET, 0);

	/* WAN: GMAC0 is connected to the PHY4 of the internal switch */
	ath79_switch_data.phy4_mii_en = 1;
	ath79_switch_data.phy_poll_mask = BIT(4);
	ath79_eth0_data.phy_if_mode = PHY_INTERFACE_MODE_MII;
	ath79_eth0_data.phy_mask = BIT(4);
	ath79_register_eth(0);

	/* LAN: GMAC1 is connected to the internal switch */
	ath79_eth1_data.phy_if_mode = PHY_INTERFACE_MODE_GMII;
	ath79_register_eth(1);
}

MIPS_MACHINE(ATH79_MACH_PW_AP603AC, "PW-AP603AC", "P&W AP603AC",
	     ap603ac_setup);
