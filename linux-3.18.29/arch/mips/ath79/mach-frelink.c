/*
 *  FRELINK board support
 *
 *  Copyright (C) 2012 Gabor Juhos <chenxj@rippletek.com>
 *
 *  This program is free software; you can redistribute it and/or modify it
 *  under the terms of the GNU General Public License version 2 as published
 *  by the Free Software Foundation.
 */

#include <linux/platform_device.h>

#include <asm/mach-ath79/ath79.h>
#include <asm/mach-ath79/ar71xx_regs.h>

#include "common.h"
#include "dev-ap9x-pci.h"
#include "dev-eth.h"
#include "dev-gpio-buttons.h"
#include "dev-leds-gpio.h"
#include "dev-m25p80.h"
#include "dev-wmac.h"
#include "machtypes.h"

#define WMAC_CALDATA_OFFSET 0x1000
#define PCIE_CALDATA_OFFSET 0x5000

#define KEYS_POLL_INTERVAL	20
#define KEYS_DEBOUNCE_INTERVAL	(3 * KEYS_POLL_INTERVAL)

#define WA111N_GPIO_BTN_RESET	17

#define WA111N_GPIO_LED_STATUS	13
#define WA111N_GPIO_LED_2G		15
#define WA111N_GPIO_LED_5G		1

#define WA111N_GPIO_LED_WAN		4
#define WA111N_GPIO_LED_LAN		16

static struct gpio_led wa111n_leds_gpio[] __initdata = {
	{
		.name		= "frelink:wan",
		.gpio		= WA111N_GPIO_LED_WAN,
		.active_low	= 1,
	}, {
		.name		= "frelink:lan",
		.gpio		= WA111N_GPIO_LED_LAN,
		.active_low	= 1,
	},{
		.name		= "frelink:2g",
		.gpio		= WA111N_GPIO_LED_2G,
		.active_low	= 1,
	},{
		.name		= "frelink:status",
		.gpio		= WA111N_GPIO_LED_STATUS,
		.active_low	= 1,
	},
};

static struct gpio_keys_button wa111n_gpio_keys[] __initdata = {
	{
		.desc		= "Reset button",
		.type		= EV_KEY,
		.code		= KEY_RESTART,
		.debounce_interval = KEYS_DEBOUNCE_INTERVAL,
		.gpio		= WA111N_GPIO_BTN_RESET,
		.active_low	= 1,
	}
};

static void __init wa111n_common(u8 *wlan0_mac, u8 *eth0_mac, u8 *eth1_mac)
{
	u8 *art = (u8 *) KSEG1ADDR(0x1fff0000);

	ath79_register_m25p80(NULL);
	ath79_register_leds_gpio(-1, ARRAY_SIZE(wa111n_leds_gpio),
			wa111n_leds_gpio);
	ath79_register_gpio_keys_polled(1, KEYS_POLL_INTERVAL,
			ARRAY_SIZE(wa111n_gpio_keys),
			wa111n_gpio_keys);

	/* Disable JTAG, enabling GPIOs 0-3 */
	/* Configure OBS4 line, for GPIO 4*/
	ath79_gpio_function_setup(AR934X_GPIO_FUNC_JTAG_DISABLE, 0);
	ath79_gpio_output_select(WA111N_GPIO_LED_STATUS, 0);
	ath79_gpio_output_select(WA111N_GPIO_LED_2G, 0);
	ath79_gpio_output_select(WA111N_GPIO_LED_WAN, 0);
	ath79_gpio_output_select(WA111N_GPIO_LED_LAN, 0);

	ath79_setup_ar933x_phy4_switch(false, false);

	ath79_register_mdio(0, 0x0);

	// wan
	ath79_switch_data.phy_poll_mask |= BIT(4);
	ath79_switch_data.phy4_mii_en = 1;
	memcpy(ath79_eth0_data.mac_addr, eth0_mac, ETH_ALEN);
	ath79_eth0_data.speed = SPEED_100;
	ath79_eth0_data.duplex = DUPLEX_FULL;
	ath79_eth0_data.phy_mask = BIT(4);
	ath79_eth0_data.phy_if_mode = PHY_INTERFACE_MODE_MII;
	ath79_register_eth(0);

	// lan
	memcpy(ath79_eth1_data.mac_addr, eth1_mac, ETH_ALEN);
	ath79_eth1_data.phy_if_mode = PHY_INTERFACE_MODE_GMII;
	ath79_eth1_data.speed = SPEED_1000;
	ath79_eth1_data.duplex = DUPLEX_FULL;
	ath79_register_eth(1);

	ath79_register_wmac(art + WMAC_CALDATA_OFFSET, wlan0_mac);
}

static void __init wa111n_setup(void)
{
	u8 *mac = (u8 *) KSEG1ADDR(0x1f040000);
	u8 wlan0_mac[ETH_ALEN];
	u8 eth0_mac[ETH_ALEN];
	u8 eth1_mac[ETH_ALEN];

	ath79_init_mac(wlan0_mac, mac + 0x630, 0);
	ath79_init_mac(eth0_mac, mac+0x610, 0);
	ath79_init_mac(eth1_mac, mac+0x620, 0);

	wa111n_common(wlan0_mac, eth0_mac, eth1_mac);
}

static void __init ax210_setup(void)
{
	u8 *mac = (u8 *) KSEG1ADDR(0x1f040000);
	u8 *art = (u8 *) KSEG1ADDR(0x1fff0000);
	u8 wlan0_mac[ETH_ALEN];
	u8 eth0_mac[ETH_ALEN];
	u8 eth1_mac[ETH_ALEN];

	ath79_init_mac(wlan0_mac, mac + 0x630, 0);
	ath79_init_mac(eth0_mac, mac+0x620, 0);
	ath79_init_mac(eth1_mac, mac+0x610, 0);

	wa111n_common(wlan0_mac, eth0_mac, eth1_mac);

	ap9x_pci_setup_wmac_led_pin(0, WA111N_GPIO_LED_5G);
	ap91_pci_init(art + PCIE_CALDATA_OFFSET, NULL);
}

MIPS_MACHINE(ATH79_MACH_RIPPLETEK_WA111N, "WA111N", "RippleTek WA111N", wa111n_setup);
MIPS_MACHINE(ATH79_MACH_XIAOBO_AX210, "XIAOBO-AX210", "XiaoBo AX210", ax210_setup);
