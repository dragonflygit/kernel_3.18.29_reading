/*
 *  COMFAST AP9341FE board support
 *
 *  Copyright (C) 2012 Gabor Juhos <chenxj@rippletek.com>
 *
 *  This program is free software; you can redistribute it and/or modify it
 *  under the terms of the GNU General Public License version 2 as published
 *  by the Free Software Foundation.
 */

#include <linux/timer.h>
#include <linux/jiffies.h>
#include <linux/gpio.h>

#include <linux/clk.h>
#include <linux/platform_device.h>

#include <asm/mach-ath79/ath79.h>
#include <asm/mach-ath79/ar71xx_regs.h>

#include "common.h"
#include "dev-ap9x-pci.h"
#include "dev-eth.h"
#include "dev-usb.h"
#include "dev-gpio-buttons.h"
#include "dev-leds-gpio.h"
#include "dev-m25p80.h"
#include "dev-wmac.h"
#include "machtypes.h"

#define WMAC_CALDATA_OFFSET 0x1000
#define PCIE_CALDATA_OFFSET 0x5000

#define KEYS_POLL_INTERVAL	20
#define KEYS_DEBOUNCE_INTERVAL	(3 * KEYS_POLL_INTERVAL)

#define AP9341FE_GPIO_LED_BLUE		0
#define AP9341FE_GPIO_LED_RED		2
#define AP9341FE_GPIO_LED_GREEEN	3

#define AP9341FE_GPIO_BTN_RESET	20

#define AP9341FE_KEYS_POLL_INTERVAL	20	/* msecs */
#define AP9341FE_KEYS_DEBOUNCE_INTERVAL (3 * AP9341FE_KEYS_POLL_INTERVAL)

#define AP9341FE_MAC0_OFFSET 0 
#define AP9341FE_MAC1_OFFSET 0x2 

static struct gpio_led ap9341fe_leds_gpio[] __initdata = {
	{
		.name		= "ap9341fe:blue",
		.gpio		= AP9341FE_GPIO_LED_BLUE,
		.active_low	= 0,
	}, {
		.name		= "ap9341fe:red",
		.gpio		= AP9341FE_GPIO_LED_RED,
		.active_low	= 0,
		.default_state  = LEDS_GPIO_DEFSTATE_ON,
	}, {
		.name		= "ap9341fe:green",
		.gpio		= AP9341FE_GPIO_LED_GREEEN,
		.active_low	= 0,
	},
};

static struct gpio_keys_button ap9341fe_gpio_keys[] __initdata = {
	{
		.desc		= "Reset button",
		.type		= EV_KEY,
		.code		= KEY_RESTART,
		.debounce_interval = AP9341FE_KEYS_DEBOUNCE_INTERVAL,
		.gpio		= AP9341FE_GPIO_BTN_RESET,
	}
};

#define	AP9341FE_GPIO_XWDT_TRIGGER	16
#define	XWDT_AUTOFEED_DURATION	 (HZ / 3)

static int gpio_external_wdt = -1;
static int wdt_timeout = -1, wdt_autofeed_count = 0;

static void watchdog_fire(unsigned long);
static struct timer_list watchdog_ticktock = TIMER_INITIALIZER(watchdog_fire, 0, 0);

static void external_wdt_toggle(void)
{
	static u32 data = 0;
	data ++;
	gpio_set_value(gpio_external_wdt, data & 0x01);
}

static void watchdog_fire(unsigned long data)
{
	if(wdt_timeout > 0) 
		wdt_autofeed_count++;

	if((wdt_timeout < 0) || (wdt_autofeed_count < wdt_timeout)) {
		external_wdt_toggle();
		mod_timer(&watchdog_ticktock, jiffies + XWDT_AUTOFEED_DURATION);
	}
}

static void enable_external_wdt(int gpio)
{
	gpio_external_wdt = gpio;
	wdt_timeout = -1;
	mod_timer(&watchdog_ticktock, jiffies + XWDT_AUTOFEED_DURATION);
}

static void ap9341fe_watchdog_init(void)
{
	enable_external_wdt(AP9341FE_GPIO_XWDT_TRIGGER);
}

static void ext_lna_control_gpio_setup(int gpio_rx0, int gpio_rx1)
{
#define AR934X_GPIO_OUT_MUX_XLNA_C0            46
#define AR934X_GPIO_OUT_MUX_XLNA_C1            47
	ath79_gpio_output_select(gpio_rx0, AR934X_GPIO_OUT_MUX_XLNA_C0);
	ath79_gpio_output_select(gpio_rx1, AR934X_GPIO_OUT_MUX_XLNA_C1);
}

static void __init ap9341fe_setup(void)
{
	u8 *mac = (u8 *) KSEG1ADDR(0x1f010000);
	u8 *art = (u8 *) KSEG1ADDR(0x1f011000);

	/* Disable JTAG, enabling GPIOs 0-3 */
	/* Configure OBS4 line, for GPIO 4*/
	ath79_gpio_function_setup(AR934X_GPIO_FUNC_JTAG_DISABLE, 
			AR934X_GPIO_FUNC_CLK_OBS4_EN);

	ath79_register_m25p80(NULL);
	ath79_register_leds_gpio(-1, ARRAY_SIZE(ap9341fe_leds_gpio),
				 ap9341fe_leds_gpio);

	ath79_register_gpio_keys_polled(1, AP9341FE_KEYS_POLL_INTERVAL,
					ARRAY_SIZE(ap9341fe_gpio_keys),
					ap9341fe_gpio_keys);

	ap9341fe_watchdog_init();

	ext_lna_control_gpio_setup(13, 14);

	ath79_setup_ar934x_eth_cfg(AR934X_ETH_CFG_SW_PHY_SWAP);

	ath79_register_mdio(1, 0x0);

	ath79_init_mac(ath79_eth0_data.mac_addr, mac, 0);
	ath79_init_mac(ath79_eth1_data.mac_addr, mac, 2);

	/* GMAC0 is connected to the PHY0 of the internal switch */
	ath79_switch_data.phy4_mii_en = 1;
	ath79_switch_data.phy_poll_mask = BIT(0);
	ath79_eth0_data.phy_if_mode = PHY_INTERFACE_MODE_MII;
	ath79_eth0_data.phy_mask = BIT(0);
	ath79_eth0_data.mii_bus_dev = &ath79_mdio1_device.dev;
	ath79_register_eth(0);

	/* GMAC1 is connected to the internal switch */
	ath79_eth1_data.phy_if_mode = PHY_INTERFACE_MODE_GMII;
	ath79_register_eth(1);

	ath79_register_wmac(art, NULL);
}

MIPS_MACHINE(ATH79_MACH_AP9341FE, "AP9341FE", "COMFAST CF-AP9341FE",
	     ap9341fe_setup);

MIPS_MACHINE(ATH79_MACH_AP8104, "AP8104", "RippleTek AP-8104",
	     ap9341fe_setup);

MIPS_MACHINE(ATH79_MACH_RT_AP9341FE, "RT-AP9341FE", "RippleTek AP9341FE",
	     ap9341fe_setup);

#define CF_WR600N_GPIO_LED_LAN1	22
#define CF_WR600N_GPIO_LED_LAN2	11
#define CF_WR600N_GPIO_LED_LAN3	19
#define CF_WR600N_GPIO_LED_WAN1	18

#define	CF_WR600N_GPIO_XWDT_TRIGGER	20

static struct gpio_keys_button comfast_cf_wr600n_gpio_keys[] __initdata = {
	{
		.desc			= "reset",
		.type			= EV_KEY,
		.code			= KEY_RESTART,
		.debounce_interval 	= AP9341FE_KEYS_DEBOUNCE_INTERVAL,
		.gpio			= 16,	
	}
};

static struct gpio_led comfast_cf_wr600n_gpio_leds[] __initdata = {
	{
		.name		= "comfast:red",
		.gpio		= 2,
	}, {
		.name		= "comfast:green",
		.gpio		= 3,
	}, {
		.name		= "comfast:blue",
		.gpio		= 0,
	}, {
		.name		= "cf_wr600n:green:lan1",
		.gpio		= CF_WR600N_GPIO_LED_LAN1,
		.active_low	= 1,
	}, {
		.name		= "cf_wr600n:green:lan2",
		.gpio		= CF_WR600N_GPIO_LED_LAN2,
		.active_low	= 1,
	}, {
		.name		= "cf_wr600n:green:lan3",
		.gpio		= CF_WR600N_GPIO_LED_LAN3,
		.active_low	= 1,
	}, {
		.name		= "cf_wr600n:green:wan1",
		.gpio		= CF_WR600N_GPIO_LED_WAN1,
		.active_low	= 1,
	},
};

static void __init cf_wr600n_setup(void)
{
	u8 *mac = (u8 *) KSEG1ADDR(0x1f010000);
	u8 *art = (u8 *) KSEG1ADDR(0x1f011000);

	/* Disable JTAG, enabling GPIOs 0-3 */
	/* Configure OBS4 line, for GPIO 4*/
	ath79_gpio_function_setup(AR934X_GPIO_FUNC_JTAG_DISABLE, AR934X_GPIO_FUNC_CLK_OBS4_EN);

	/* ath79_gpio_output_select(CF_WR600N_GPIO_XWDT_TRIGGER, 0); */
	/* enable_external_wdt(CF_WR600N_GPIO_XWDT_TRIGGER); */

	ath79_register_m25p80(NULL);

	ath79_gpio_output_select(CF_WR600N_GPIO_LED_LAN2, 0);
	
	ath79_register_leds_gpio(-1, ARRAY_SIZE(comfast_cf_wr600n_gpio_leds),
				 comfast_cf_wr600n_gpio_leds);
	ath79_register_gpio_keys_polled(-1, AP9341FE_KEYS_POLL_INTERVAL,
                                        ARRAY_SIZE(comfast_cf_wr600n_gpio_keys),
                                        comfast_cf_wr600n_gpio_keys);
					
	ext_lna_control_gpio_setup(13, 14);
	
	ath79_register_mdio(1, 0x0);

	ath79_init_mac(ath79_eth0_data.mac_addr, mac, 0);
	ath79_init_mac(ath79_eth1_data.mac_addr, mac, 2);

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
	
	//ath79_register_usb();

	ath79_register_wmac(art, NULL);
} 

MIPS_MACHINE(ATH79_MACH_CF_WR600N, "CF-WR600N", "COMFAST CF-WR600N", 
		cf_wr600n_setup);

#define CF_WR605N_GPIO_LED_WAN_EXT	15
#define CF_WR605N_GPIO_LED_LAN_EXT	17
#define CF_WR605N_GPIO_LED_WIFI_EXT	20

#define	CF_WR605N_GPIO_XWDT_TRIGGER	20

static struct gpio_keys_button comfast_cf_wr605n_gpio_keys[] __initdata = {
	{
		.desc			= "reset",
		.type			= EV_KEY,
		.code			= KEY_RESTART,
		.debounce_interval	= AP9341FE_KEYS_DEBOUNCE_INTERVAL,
		.gpio			= 16,	
	}
};

static struct gpio_led comfast_cf_wr605n_gpio_leds[] __initdata = {
	{
		.name		= "cf_wr605n:white:wan",
		.gpio		= CF_WR605N_GPIO_LED_WAN_EXT,
		.active_low	= 1,
	}, {
		.name		= "cf_wr605n:white:lan",
		.gpio		= CF_WR605N_GPIO_LED_LAN_EXT,
		.active_low	= 1,
	}, {
		.name		= "cf_wr605n:white:wifi",
		.gpio		= CF_WR605N_GPIO_LED_WIFI_EXT,
		.active_low	= 1,
	},
};


static void __init cf_wr605n_setup(void)
{
	u8 *mac = (u8 *) KSEG1ADDR(0x1f010000);
	u8 *art = (u8 *) KSEG1ADDR(0x1f011000);

	/* Disable JTAG, enabling GPIOs 0-3 */
	/* Configure OBS4 line, for GPIO 4*/
	ath79_gpio_function_setup(AR934X_GPIO_FUNC_JTAG_DISABLE, AR934X_GPIO_FUNC_CLK_OBS4_EN);

	//	ath79_gpio_output_select(CF_WR605N_GPIO_XWDT_TRIGGER, 0);
	//	enable_external_wdt(CF_WR605N_GPIO_XWDT_TRIGGER);

	ath79_register_m25p80(NULL);

	ath79_gpio_output_select(CF_WR605N_GPIO_LED_WAN_EXT, 0);
	ath79_gpio_output_select(CF_WR605N_GPIO_LED_LAN_EXT, 0);
	ath79_gpio_output_select(CF_WR605N_GPIO_LED_WIFI_EXT, 0);

	ath79_register_leds_gpio(-1, ARRAY_SIZE(comfast_cf_wr605n_gpio_leds),
			comfast_cf_wr605n_gpio_leds);
	ath79_register_gpio_keys_polled(-1, AP9341FE_KEYS_POLL_INTERVAL,
			ARRAY_SIZE(comfast_cf_wr605n_gpio_keys),
			comfast_cf_wr605n_gpio_keys);

	ext_lna_control_gpio_setup(13, 14);

	ath79_register_mdio(1, 0x0);

	ath79_init_mac(ath79_eth0_data.mac_addr, mac, 0);
	ath79_init_mac(ath79_eth1_data.mac_addr, mac, 2);

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

	//ath79_register_usb();

	ath79_register_wmac(art, NULL);
} 

MIPS_MACHINE(ATH79_MACH_CF_WR605N, "CF-WR605N", "COMFAST CF-WR605N",
		cf_wr605n_setup);

static struct gpio_led comfast_cf_e316nv2_gpio_leds[] __initdata = {
	{
		.name		= "comfast:white:wifi",
		.gpio		= 12,
		.active_low	= 1,
	}, {
		.name		= "comfast:white:lan",
		.gpio		= 19,
		.active_low	= 1,
	}, {
		.name		= "comfast:white:wan",
		.gpio		= 17,
		.active_low	= 1,
	}, {
		.name		= "comfast:green",
		.gpio		= 3,
	}
};

static void __init e316n_setup(void)
{
	u8 *mac = (u8 *) KSEG1ADDR(0x1f010000);
	u8 *art = (u8 *) KSEG1ADDR(0x1f011000);

	/* Disable JTAG, enabling GPIOs 0-3 */
	/* Configure OBS4 line, for GPIO 4*/
	ath79_gpio_function_setup(AR934X_GPIO_FUNC_JTAG_DISABLE, AR934X_GPIO_FUNC_CLK_OBS4_EN);

	ath79_gpio_output_select(AP9341FE_GPIO_XWDT_TRIGGER, 0);
	enable_external_wdt(AP9341FE_GPIO_XWDT_TRIGGER);

	ath79_register_m25p80(NULL);
	
	ath79_gpio_output_select(12, 0);
	ath79_gpio_output_select(17, 0);
	ath79_gpio_output_select(19, 0);

	ath79_register_leds_gpio(-1, ARRAY_SIZE(comfast_cf_e316nv2_gpio_leds),
				 comfast_cf_e316nv2_gpio_leds);

	ath79_register_gpio_keys_polled(1, AP9341FE_KEYS_POLL_INTERVAL,
					ARRAY_SIZE(ap9341fe_gpio_keys),
					ap9341fe_gpio_keys);

	ext_lna_control_gpio_setup(13, 14);

	ath79_setup_ar934x_eth_cfg(AR934X_ETH_CFG_SW_PHY_SWAP);

	ath79_register_mdio(1, 0x0);

	ath79_init_mac(ath79_eth0_data.mac_addr, mac, 0);
	ath79_init_mac(ath79_eth1_data.mac_addr, mac, 2);

	/* GMAC0 is connected to the PHY0 of the internal switch */
	ath79_switch_data.phy4_mii_en = 1;
	ath79_switch_data.phy_poll_mask = BIT(0);
	ath79_eth0_data.phy_if_mode = PHY_INTERFACE_MODE_MII;
	ath79_eth0_data.phy_mask = BIT(0);
	ath79_eth0_data.mii_bus_dev = &ath79_mdio1_device.dev;
	ath79_register_eth(0);

	/* GMAC1 is connected to the internal switch */
	ath79_eth1_data.phy_if_mode = PHY_INTERFACE_MODE_GMII;	
	
	ath79_register_eth(1);
	
	//ath79_register_usb();

	ath79_register_wmac(art, NULL);
}

MIPS_MACHINE(ATH79_MACH_CF_E316N, "CF-E316N", "COMFAST CF-E316N",
	     e316n_setup);

MIPS_MACHINE(ATH79_MACH_AP316N, "AP316N", "RippleTek AP316N",
	     e316n_setup);

#define CF_WR610N_GPIO_BTN_RESET	17

#define CF_WR610N_KEYS_POLL_INTERVAL	20	/* msecs */
#define CF_WR610N_KEYS_DEBOUNCE_INTERVAL (3 * CF_WR610N_KEYS_POLL_INTERVAL)

#define	CF_WR610N_GPIO_XWDT_TRIGGER	13

#define CF_WR610N_GPIO_LED_RED		2
#define CF_WR610N_GPIO_LED_GREEN	3
#define CF_WR610N_GPIO_LED_BLUE		0

#define CF_WR610N_GPIO_LED_WAN		4
#define CF_WR610N_GPIO_LED_LAN1		16
#define CF_WR610N_GPIO_LED_LAN2		15
#define CF_WR610N_GPIO_LED_LAN3		14
#define CF_WR610N_GPIO_LED_LAN4		11


static struct gpio_led cf_wr610n_leds_gpio[] __initdata = {
	{
		.name		= "comfast:red",
		.gpio		= CF_WR610N_GPIO_LED_RED,
		.active_low	= 1,
	}, {
		.name		= "comfast:green",
		.gpio		= CF_WR610N_GPIO_LED_GREEN,
		.active_low	= 0,
	}, {
		.name		= "comfast:blue",
		.gpio		= CF_WR610N_GPIO_LED_BLUE,
		.active_low	= 0,
	}, {
		.name		= "comfast:green:wan",
		.gpio		= CF_WR610N_GPIO_LED_WAN,
		.active_low	= 1,
	}, {
		.name		= "comfast:green:lan1",
		.gpio		= CF_WR610N_GPIO_LED_LAN1,
		.active_low	= 1,
	}, {
		.name		= "comfast:green:lan2",
		.gpio		= CF_WR610N_GPIO_LED_LAN2,
		.active_low	= 1,
	}, {
		.name		= "comfast:green:lan3",
		.gpio		= CF_WR610N_GPIO_LED_LAN3,
		.active_low	= 1,
	}, {
		.name		= "comfast:green:lan4",
		.gpio		= CF_WR610N_GPIO_LED_LAN4,
		.active_low	= 1,
	},
};

static struct gpio_keys_button cf_wr610n_gpio_keys[] __initdata = {
	{
		.desc		= "Reset button",
		.type		= EV_KEY,
		.code		= KEY_RESTART,
		.debounce_interval = CF_WR610N_KEYS_DEBOUNCE_INTERVAL,
		.gpio		= CF_WR610N_GPIO_BTN_RESET,
		.active_low	= 0,
	}
};

static void __init comfast_ap143_setup(void)
{
	u8 *mac = (u8 *) KSEG1ADDR(0x1f010000);
	u8 *art = (u8 *) KSEG1ADDR(0x1f011000);

	/* Disable JTAG, enabling GPIOs 0-3 */
	/* Configure OBS4 line, for GPIO 4*/
	ath79_gpio_function_setup(AR934X_GPIO_FUNC_JTAG_DISABLE, 0);

	ath79_gpio_output_select(CF_WR610N_GPIO_XWDT_TRIGGER, 0);
	enable_external_wdt(CF_WR610N_GPIO_XWDT_TRIGGER);

	ath79_gpio_output_select(CF_WR610N_GPIO_LED_RED, 0);
	ath79_gpio_output_select(CF_WR610N_GPIO_LED_GREEN, 0);
	ath79_gpio_output_select(CF_WR610N_GPIO_LED_BLUE, 0);

	ath79_gpio_output_select(CF_WR610N_GPIO_LED_WAN, 0);
	ath79_gpio_output_select(CF_WR610N_GPIO_LED_LAN1, 0);
	ath79_gpio_output_select(CF_WR610N_GPIO_LED_LAN2, 0);
	ath79_gpio_output_select(CF_WR610N_GPIO_LED_LAN3, 0);
	ath79_gpio_output_select(CF_WR610N_GPIO_LED_LAN4, 0);

	ath79_register_m25p80(NULL);

	ath79_setup_ar933x_phy4_switch(false, false);

	ath79_register_mdio(0, 0x0);
	ath79_register_mdio(1, 0x0);

	ath79_init_mac(ath79_eth0_data.mac_addr, mac, 0);
	ath79_init_mac(ath79_eth1_data.mac_addr, mac, 2);

	// lan
	ath79_eth1_data.phy_if_mode = PHY_INTERFACE_MODE_GMII;
	ath79_eth1_data.speed = SPEED_1000;
	ath79_eth1_data.duplex = DUPLEX_FULL;
	ath79_switch_data.phy_poll_mask |= BIT(4);
	ath79_switch_data.phy4_mii_en = 1;
	ath79_register_eth(1);

	// wan
	ath79_eth0_data.phy_if_mode = PHY_INTERFACE_MODE_MII;
	ath79_eth0_data.speed = SPEED_100;
	ath79_eth0_data.duplex = DUPLEX_FULL;
	ath79_eth0_data.phy_mask = BIT(4);
	ath79_register_eth(0);

	ath79_register_wmac(art, NULL);
}

static void __init cf_wr610n_setup(void)
{
	comfast_ap143_setup();

	ath79_register_leds_gpio(-1, ARRAY_SIZE(cf_wr610n_leds_gpio),
				 cf_wr610n_leds_gpio);

	ath79_register_gpio_keys_polled(1, CF_WR610N_KEYS_POLL_INTERVAL,
					ARRAY_SIZE(cf_wr610n_gpio_keys),
					cf_wr610n_gpio_keys);
}

MIPS_MACHINE(ATH79_MACH_CF_WR610N, "CF-WR610N", "COMFAST CF-WR610N",
	     cf_wr610n_setup);

#define CF_E350N_GPIO_BTN_RESET	17

#define CF_E350N_KEYS_POLL_INTERVAL	20	/* msecs */
#define CF_E350N_KEYS_DEBOUNCE_INTERVAL (3 * CF_E350N_KEYS_POLL_INTERVAL)

#define CF_E350N_GPIO_XWDT_TRIGGER	13

#define CF_E350N_GPIO_LED_WAN		2
#define CF_E350N_GPIO_LED_LAN		3
#define CF_E350N_GPIO_LED_WLAN		0

#define CF_E350N_GPIO_LED_LAN1        11
#define CF_E350N_GPIO_LED_LAN2        12
#define CF_E350N_GPIO_LED_LAN3        14
#define CF_E350N_GPIO_LED_LAN4        16

#define CF_E350N_GPIO_SDA        15
#define CF_E350N_GPIO_SCL        4

static struct gpio_led cf_e350n_leds_gpio[] __initdata = {
	{
		.name		= "comfast:red:wan",
		.gpio		= CF_E350N_GPIO_LED_WAN,
		.active_low	= 0,
	}, {
		.name		= "comfast:green:lan",
		.gpio		= CF_E350N_GPIO_LED_LAN,
		.active_low	= 0,
	}, {
		.name		= "comfast:blue:wlan",
		.gpio		= CF_E350N_GPIO_LED_WLAN,
		.active_low	= 0,
	}, {
		.name		= "comfast:green:lan1",
		.gpio		= CF_E350N_GPIO_LED_LAN1,
		.active_low	= 1,
	}, {
		.name		= "comfast:green:lan2",
		.gpio		= CF_E350N_GPIO_LED_LAN2,
		.active_low	= 1,
	}, {
		.name		= "comfast:green:lan3",
		.gpio		= CF_E350N_GPIO_LED_LAN3,
		.active_low	= 1,
	}, {
		.name		= "comfast:green:lan4",
		.gpio		= CF_E350N_GPIO_LED_LAN4,
		.active_low	= 1,
	},
};

static struct gpio_keys_button cf_e350n_gpio_keys[] __initdata = {
	{
		.desc		= "Reset button",
		.type		= EV_KEY,
		.code		= KEY_RESTART,
		.debounce_interval = CF_E350N_KEYS_DEBOUNCE_INTERVAL,
		.gpio		= CF_E350N_GPIO_BTN_RESET,
		.active_low	= 0,
	}
};

static void __init comfast_cf_e350n_setup(void)
{
	u8 *mac = (u8 *) KSEG1ADDR(0x1f010000);
	u8 *art = (u8 *) KSEG1ADDR(0x1f010000);

	/* Disable JTAG, enabling GPIOs 0-3 */
	/* Configure OBS4 line, for GPIO 4*/	
	ath79_gpio_function_setup(AR934X_GPIO_FUNC_JTAG_DISABLE, 0);	

	ath79_gpio_output_select(CF_E350N_GPIO_XWDT_TRIGGER, 0);	
	enable_external_wdt(CF_E350N_GPIO_XWDT_TRIGGER);

	ath79_gpio_output_select(CF_E350N_GPIO_LED_WAN, 0);
	ath79_gpio_output_select(CF_E350N_GPIO_LED_LAN, 0);
	ath79_gpio_output_select(CF_E350N_GPIO_LED_WLAN, 0);	

	ath79_gpio_output_select(CF_E350N_GPIO_LED_LAN1, 0);
	ath79_gpio_output_select(CF_E350N_GPIO_LED_LAN2, 0);
	ath79_gpio_output_select(CF_E350N_GPIO_LED_LAN3, 0);
	ath79_gpio_output_select(CF_E350N_GPIO_LED_LAN4, 0);
	
	ath79_gpio_output_select(CF_E350N_GPIO_SDA, 0);
	ath79_gpio_output_select(CF_E350N_GPIO_SCL, 0);

	ath79_register_m25p80(NULL);

	ath79_setup_ar933x_phy4_switch(false, false);

	ath79_register_mdio(0, 0x0);

	ath79_init_mac(ath79_eth1_data.mac_addr, mac, 0);
	ath79_init_mac(ath79_eth0_data.mac_addr, mac, 2);

	// wan
	ath79_switch_data.phy_poll_mask |= BIT(4);
	ath79_switch_data.phy4_mii_en = 1;
	ath79_eth0_data.speed = SPEED_100;
	ath79_eth0_data.duplex = DUPLEX_FULL;
	ath79_eth0_data.phy_mask = BIT(4);
	ath79_eth0_data.phy_if_mode = PHY_INTERFACE_MODE_MII;
	ath79_register_eth(0);

	// lan
	ath79_eth1_data.phy_if_mode = PHY_INTERFACE_MODE_GMII;
	ath79_register_eth(1);

	ath79_register_wmac(art + WMAC_CALDATA_OFFSET, NULL);

	ap91_pci_init(art + PCIE_CALDATA_OFFSET, NULL);
}

static void __init cf_e350n_setup(void)
{
	comfast_cf_e350n_setup();

	ath79_register_leds_gpio(-1, ARRAY_SIZE(cf_e350n_leds_gpio),
			cf_e350n_leds_gpio);

	ath79_register_gpio_keys_polled(1, CF_E350N_KEYS_POLL_INTERVAL,
			ARRAY_SIZE(cf_e350n_gpio_keys),
			cf_e350n_gpio_keys);
}

MIPS_MACHINE(ATH79_MACH_CF_E350N, "CF-E350N", "COMFAST CF-E350N",
		cf_e350n_setup);
MIPS_MACHINE(ATH79_MACH_COMFAST_CF_E320NV2, "CF-E320NV2", "COMFAST CF-E320NV2",
	     cf_e350n_setup);

static struct gpio_led cf_e314n_leds_gpio[] __initdata = {
	{
		.name		= "comfast:red:wan",
		.gpio		= CF_E350N_GPIO_LED_WAN,
		.active_low	= 1,
	}, {
		.name		= "comfast:green:lan",
		.gpio		= CF_E350N_GPIO_LED_LAN,
		.active_low	= 1,
	}, {
		.name		= "comfast:blue:wlan",
		.gpio		= CF_E350N_GPIO_LED_WLAN,
		.active_low	= 1,
	}, {
		.name		= "comfast:green:lan1",
		.gpio		= CF_E350N_GPIO_LED_LAN1,
		.active_low	= 1,
	}, {
		.name		= "comfast:green:lan2",
		.gpio		= CF_E350N_GPIO_LED_LAN2,
		.active_low	= 1,
	}, {
		.name		= "comfast:green:lan3",
		.gpio		= CF_E350N_GPIO_LED_LAN3,
		.active_low	= 1,
	}, {
		.name		= "comfast:green:lan4",
		.gpio		= CF_E350N_GPIO_LED_LAN4,
		.active_low	= 1,
	}, {
		.name		= "cf_e350n:sda",
		.gpio		= CF_E350N_GPIO_SDA,
		.active_low	= 1,
	}, {
		.name		= "cf_e350n:scl",
		.gpio		= CF_E350N_GPIO_SCL,
		.active_low	= 1, 
	}, 
};

static void __init cf_e314n_setup(void)
{
	comfast_cf_e350n_setup();

	ath79_register_leds_gpio(-1, ARRAY_SIZE(cf_e314n_leds_gpio),
				 cf_e314n_leds_gpio);

	ath79_register_gpio_keys_polled(1, KEYS_POLL_INTERVAL,
					ARRAY_SIZE(cf_e350n_gpio_keys),
					cf_e350n_gpio_keys);
}

MIPS_MACHINE(ATH79_MACH_COMFAST_CF_E314N, "CF-E314N", "COMFAST CF-E314N",
	     cf_e314n_setup);

#define CF_E520N_GPIO_BTN_RESET	17

#define CF_E520N_KEYS_POLL_INTERVAL	20	/* msecs */
#define CF_E520N_KEYS_DEBOUNCE_INTERVAL (3 * CF_E520N_KEYS_POLL_INTERVAL)

#define CF_E520N_GPIO_LED_WAN		11

static struct gpio_led cf_e520n_leds_gpio[] __initdata = {
	{
		.name		= "comfast:blue:wan",
		.gpio		= CF_E520N_GPIO_LED_WAN,
		.active_low	= 1,
	}
};

static struct gpio_keys_button cf_e520n_gpio_keys[] __initdata = {
	{
		.desc		= "Reset button",
		.type		= EV_KEY,
		.code		= KEY_RESTART,
		.debounce_interval = CF_E520N_KEYS_DEBOUNCE_INTERVAL,
		.gpio		= CF_E520N_GPIO_BTN_RESET,
		.active_low	= 0,
	}
};

static void __init comfast_e520n_setup(void)
{
	u8 *mac = (u8 *) KSEG1ADDR(0x1f010000);
	u8 *art = (u8 *) KSEG1ADDR(0x1f011000);

	/* Disable JTAG, enabling GPIOs 0-3 */
	/* Configure OBS4 line, for GPIO 4*/	
	ath79_gpio_function_setup(AR934X_GPIO_FUNC_JTAG_DISABLE, 0);	

	ath79_gpio_output_select(CF_E520N_GPIO_LED_WAN, 0);

	ath79_register_m25p80(NULL);

	ath79_setup_ar933x_phy4_switch(false, false);

	ath79_register_mdio(0, 0x0);
	ath79_register_mdio(1, 0x0);

	ath79_init_mac(ath79_eth1_data.mac_addr, mac, 0);
	ath79_init_mac(ath79_eth0_data.mac_addr, mac, 2);

	// wan
	ath79_switch_data.phy_poll_mask |= BIT(4);
	ath79_switch_data.phy4_mii_en = 1;
	ath79_eth0_data.speed = SPEED_100;
	ath79_eth0_data.duplex = DUPLEX_FULL;
	ath79_eth0_data.phy_mask = BIT(4);
	ath79_eth0_data.phy_if_mode = PHY_INTERFACE_MODE_MII;
	ath79_register_eth(0);

	// lan
	ath79_eth1_data.phy_if_mode = PHY_INTERFACE_MODE_GMII;
	ath79_register_eth(1);

	ath79_register_wmac(art, NULL);	
}

static void __init cf_e520n_e530n_setup(void)
{
	comfast_e520n_setup();

	ath79_register_leds_gpio(-1, ARRAY_SIZE(cf_e520n_leds_gpio),
			cf_e520n_leds_gpio);

	ath79_register_gpio_keys_polled(1, CF_E520N_KEYS_POLL_INTERVAL,
			ARRAY_SIZE(cf_e520n_gpio_keys),
			cf_e520n_gpio_keys);
}
MIPS_MACHINE(ATH79_MACH_CF_E520N, "CF-E520N", "COMFAST CF-E520N",
		cf_e520n_e530n_setup);
MIPS_MACHINE(ATH79_MACH_CF_E530N, "CF-E530N", "COMFAST CF-E530N",
		cf_e520n_e530n_setup);

#define CF_E312A_GPIO_LED_SIGNAL1	14
#define CF_E312A_GPIO_LED_SIGNAL2	15
#define CF_E312A_GPIO_LED_SIGNAL3	16
#define CF_E312A_GPIO_LED_SIGNAL4	17

#define CF_E312A_GPIO_SDA		11
#define CF_E312A_GPIO_SCL		12

#define	CF_E312A_GPIO_XWDT_TRIGGER	19

static struct gpio_keys_button comfast_cf_e312a_gpio_keys[] __initdata = {
	{
		.desc			= "reset",
		.type			= EV_KEY,
		.code			= KEY_RESTART,
		.debounce_interval	= KEYS_DEBOUNCE_INTERVAL,
		.gpio			= 22,	
		.active_low		= 0,
	}
};

static struct gpio_led comfast_cf_e312a_gpio_leds[] __initdata = {
	{
		.name		= "comfast:red",
		.gpio		= 2,
		.active_low	= 1,
	}, {
		.name		= "comfast:green",
		.gpio		= 3,
		.active_low	= 1,
	}, {
		.name		= "comfast:blue",
		.gpio		= 0,
		.active_low	= 1,
	}, {
		.name		= "cf_e312a:signal1",
		.gpio		= CF_E312A_GPIO_LED_SIGNAL1,
		.active_low	= 1,
	}, {
		.name		= "cf_e312a:signal2",
		.gpio		= CF_E312A_GPIO_LED_SIGNAL2,
		.active_low	= 1,
	}, {
		.name		= "cf_e312a:signal3",
		.gpio		= CF_E312A_GPIO_LED_SIGNAL3,
		.active_low	= 1,
	}, {
		.name		= "cf_e312a:signal4",
		.gpio		= CF_E312A_GPIO_LED_SIGNAL4,
		.active_low	= 1,
	}, {
		.name		= "cf_e312a:sda",
		.gpio		= CF_E312A_GPIO_SDA,
		.active_low	= 1,
	}, {
		.name		= "cf_e312a:scl",
		.gpio		= CF_E312A_GPIO_SCL,
		.active_low	= 1,		
	}
};

static void __init comfast_cf_e312a_setup(void)
{
	u8 *mac = (u8 *) KSEG1ADDR(0x1f010000);
	u8 *art = (u8 *) KSEG1ADDR(0x1f011000);

	/* Disable JTAG, enabling GPIOs 0-3 */
	/* Configure OBS4 line, for GPIO 4*/
	ath79_gpio_function_setup(AR934X_GPIO_FUNC_JTAG_DISABLE, AR934X_GPIO_FUNC_CLK_OBS4_EN);

	ath79_gpio_output_select(CF_E312A_GPIO_XWDT_TRIGGER, 0);
	enable_external_wdt(CF_E312A_GPIO_XWDT_TRIGGER);

	ath79_register_m25p80(NULL);
	
	ath79_gpio_output_select(CF_E312A_GPIO_LED_SIGNAL1, 0);
	ath79_gpio_output_select(CF_E312A_GPIO_LED_SIGNAL2, 0);
	ath79_gpio_output_select(CF_E312A_GPIO_LED_SIGNAL3, 0);
	ath79_gpio_output_select(CF_E312A_GPIO_LED_SIGNAL4, 0);

	ath79_gpio_output_select(CF_E312A_GPIO_SDA, 0);
	ath79_gpio_output_select(CF_E312A_GPIO_SCL, 0);	

	ath79_register_leds_gpio(-1, ARRAY_SIZE(comfast_cf_e312a_gpio_leds),
				 comfast_cf_e312a_gpio_leds);
	ath79_register_gpio_keys_polled(-1, KEYS_POLL_INTERVAL,
                                        ARRAY_SIZE(comfast_cf_e312a_gpio_keys),
                                        comfast_cf_e312a_gpio_keys);

	ath79_setup_ar934x_eth_cfg(AR934X_ETH_CFG_SW_PHY_SWAP);

	ath79_register_mdio(1, 0x0);

	/* GMAC1 is connected to the internal switch, LAN */
	ath79_init_mac(ath79_eth1_data.mac_addr, mac, 0);
	ath79_eth1_data.phy_if_mode = PHY_INTERFACE_MODE_GMII;	
	ath79_register_eth(1);
	
	/* GMAC0 is connected to the PHY0 of the internal switch, WAN */
	ath79_init_mac(ath79_eth0_data.mac_addr, mac, 2);
	ath79_switch_data.phy4_mii_en = 1;
	ath79_switch_data.phy_poll_mask = BIT(0);
	ath79_eth0_data.phy_if_mode = PHY_INTERFACE_MODE_MII;
	ath79_eth0_data.phy_mask = BIT(0);
	ath79_eth0_data.mii_bus_dev = &ath79_mdio1_device.dev;
	ath79_register_eth(0);

	ath79_register_wmac(art, NULL);
} 

MIPS_MACHINE(ATH79_MACH_COMFAST_CF_E312A, "CF-E312A", "COMFAST CF-E312A", 
	comfast_cf_e312a_setup);

#define CF_E355AC_GPIO_BTN_RESET	17

#define	CF_E355AC_GPIO_XWDT_TRIGGER	13

#define CF_E355AC_GPIO_LED_WAN		2
#define CF_E355AC_GPIO_LED_LAN		3
#define CF_E355AC_GPIO_LED_WLAN		0

#define CF_E355AC_GPIO_LED_EXT_WAN		16
#define CF_E355AC_GPIO_LED_EXT_LAN		14
#define CF_E355AC_GPIO_LED_EXT_WLAN		11

static struct gpio_led cf_e355ac_leds_gpio[] __initdata = {
	{
		.name		= "comfast:red:wan",
		.gpio		= CF_E355AC_GPIO_LED_WAN,
		.active_low	= 0,
	}, {
		.name		= "comfast:green:lan",
		.gpio		= CF_E355AC_GPIO_LED_LAN,
		.active_low	= 0,
	}, {
		.name		= "comfast:blue:wlan",
		.gpio		= CF_E355AC_GPIO_LED_WLAN,
		.active_low	= 0,
	}, {
		.name		= "comfast:blue:ext_wan",
		.gpio		= CF_E355AC_GPIO_LED_EXT_WAN,
		.active_low	= 1,
	}, {
		.name		= "comfast:blue:ext_lan",
		.gpio		= CF_E355AC_GPIO_LED_EXT_LAN,
		.active_low	= 1,
	}, {
		.name		= "comfast:blue:ext_wlan",
		.gpio		= CF_E355AC_GPIO_LED_EXT_WLAN,
		.active_low	= 1,
	}, 
};

static struct gpio_keys_button cf_e355ac_gpio_keys[] __initdata = {
	{
		.desc		= "Reset button",
		.type		= EV_KEY,
		.code		= KEY_RESTART,
		.debounce_interval = KEYS_DEBOUNCE_INTERVAL,
		.gpio		= CF_E355AC_GPIO_BTN_RESET,
		.active_low	= 1,
	}
};

static void __init comfast_cf_e355ac_setup(void)
{
	u8 *mac = (u8 *) KSEG1ADDR(0x1f010000);
	u8 *art = (u8 *) KSEG1ADDR(0x1f010000);
	
	/* Disable JTAG, enabling GPIOs 0-3 */
	/* Configure OBS4 line, for GPIO 4*/	
	ath79_gpio_function_setup(AR934X_GPIO_FUNC_JTAG_DISABLE, 0);	
	
	ath79_gpio_output_select(CF_E355AC_GPIO_XWDT_TRIGGER, 0);	
	enable_external_wdt(CF_E355AC_GPIO_XWDT_TRIGGER);
	
	ath79_gpio_output_select(CF_E355AC_GPIO_LED_WAN, 0);
	ath79_gpio_output_select(CF_E355AC_GPIO_LED_LAN, 0);
	ath79_gpio_output_select(CF_E355AC_GPIO_LED_WLAN, 0);	

	ath79_gpio_output_select(CF_E355AC_GPIO_LED_EXT_WAN, 0);
	ath79_gpio_output_select(CF_E355AC_GPIO_LED_EXT_LAN, 0);
	ath79_gpio_output_select(CF_E355AC_GPIO_LED_EXT_WLAN, 0);

	ath79_register_m25p80(NULL);

	ath79_setup_ar933x_phy4_switch(false, false);

	ath79_register_mdio(0, 0x0);
	
	ath79_init_mac(ath79_eth0_data.mac_addr, mac, 2);
	ath79_init_mac(ath79_eth1_data.mac_addr, mac, 0);

	// lan
	ath79_eth1_data.phy_if_mode = PHY_INTERFACE_MODE_GMII;
	ath79_eth1_data.speed = SPEED_1000;
	ath79_eth1_data.duplex = DUPLEX_FULL;
	ath79_register_eth(1);

	// wan
	ath79_switch_data.phy4_mii_en = 1;
	ath79_switch_data.phy_poll_mask = BIT(4);
	ath79_eth0_data.phy_if_mode = PHY_INTERFACE_MODE_MII;
	ath79_eth0_data.phy_mask = BIT(4);
	ath79_eth0_data.speed = SPEED_100;
	ath79_eth0_data.duplex = DUPLEX_FULL;
	ath79_register_eth(0);

	ath79_register_wmac(art + 0x1000, NULL);
	
	ap91_pci_init(art + 0x5000, NULL);
}

static void __init cf_e355ac_setup(void)
{
	comfast_cf_e355ac_setup();

	ath79_register_leds_gpio(-1, ARRAY_SIZE(cf_e355ac_leds_gpio),
				 cf_e355ac_leds_gpio);

	ath79_register_gpio_keys_polled(1, KEYS_POLL_INTERVAL,
					ARRAY_SIZE(cf_e355ac_gpio_keys),
					cf_e355ac_gpio_keys);
}

MIPS_MACHINE(ATH79_MACH_CF_E355AC, "CF-E355AC", "COMFAST CF-E355AC", cf_e355ac_setup);
MIPS_MACHINE(ATH79_MACH_CF_E351AC, "CF-E351AC", "COMFAST CF-E351AC", cf_e355ac_setup);
MIPS_MACHINE(ATH79_MACH_XIAOBO_AX200, "XIAOBO-AX200", "XiaoBo AX200", cf_e355ac_setup);
MIPS_MACHINE(ATH79_MACH_XIAOBO_EN8720AX, "XIAOBO-EN8720AX", "XiaoBo EN8720AX", cf_e355ac_setup);

#define CF_WR630AC_GPIO_BTN_RESET	17
#define CF_WR630AC_GPIO_XWDT_TRIGGER	13

#define CF_WR630AC_GPIO_LED_58G		12
#define CF_WR630AC_GPIO_LED_NETWORK	3
#define CF_WR630AC_GPIO_LED_WPS		1
#define CF_WR630AC_GPIO_LED_24G		2

#define CF_WR630AC_GPIO_LED_WAN1	4
#define CF_WR630AC_GPIO_LED_LAN1	16
#define CF_WR630AC_GPIO_LED_LAN2	15
#define CF_WR630AC_GPIO_LED_LAN3	14
#define CF_WR630AC_GPIO_LED_LAN4	11

static struct gpio_led cf_wr630ac_leds_gpio[] __initdata = {
	{
		.name		= "comfast:green:wan1",
		.gpio		= CF_WR630AC_GPIO_LED_WAN1,
		.active_low	= 1,
	}, {
		.name		= "comfast:green:lan1",
		.gpio		= CF_WR630AC_GPIO_LED_LAN1,
		.active_low	= 1,
	}, {
		.name		= "comfast:green:lan2",
		.gpio		= CF_WR630AC_GPIO_LED_LAN2,
		.active_low	= 1,
	}, {
		.name		= "comfast:green:lan3",
		.gpio		= CF_WR630AC_GPIO_LED_LAN3,
		.active_low	= 1,
	}, {
		.name		= "comfast:green:lan4",
		.gpio		= CF_WR630AC_GPIO_LED_LAN4,
		.active_low	= 1,
	},{
		.name		= "comfast:blue:24g",
		.gpio		= CF_WR630AC_GPIO_LED_24G,
		.active_low	= 1,
	}, {
		.name		= "comfast:blue:wps",
		.gpio		= CF_WR630AC_GPIO_LED_WPS,
		.active_low	= 1,
	},{
		.name		= "comfast:blue:58g",
		.gpio		= CF_WR630AC_GPIO_LED_58G,
		.active_low	= 1,
	}, {
		.name		= "comfast:blue:network",
		.gpio		= CF_WR630AC_GPIO_LED_NETWORK,
		.active_low	= 1,
	},
};

static struct gpio_keys_button cf_wr630ac_gpio_keys[] __initdata = {
	{
		.desc		= "Reset button",
		.type		= EV_KEY,
		.code		= KEY_RESTART,
		.debounce_interval = KEYS_DEBOUNCE_INTERVAL,
		.gpio		= CF_WR630AC_GPIO_BTN_RESET,
		.active_low	= 1,
	}
};

static void __init comfast_wr630ac_setup(void)
{
	u8 *mac = (u8 *) KSEG1ADDR(0x1f010000);
	u8 *art = (u8 *) KSEG1ADDR(0x1f010000);
	u8 wlan0_mac[ETH_ALEN];
	u8 wlan1_mac[ETH_ALEN];

	ath79_init_mac(wlan0_mac, art, 2);
	ath79_init_mac(wlan1_mac, art, 3);

	ath79_gpio_output_select(CF_WR630AC_GPIO_XWDT_TRIGGER, 0);
	enable_external_wdt(CF_WR630AC_GPIO_XWDT_TRIGGER);

	/* Disable JTAG, enabling GPIOs 0-3 */
	/* Configure OBS4 line, for GPIO 4*/	
	ath79_gpio_function_setup(AR934X_GPIO_FUNC_JTAG_DISABLE, 0);	

	ath79_gpio_output_select(CF_WR630AC_GPIO_LED_NETWORK, 0);
	ath79_gpio_output_select(CF_WR630AC_GPIO_LED_58G, 0);	
	ath79_gpio_output_select(CF_WR630AC_GPIO_LED_24G, 0);
	ath79_gpio_output_select(CF_WR630AC_GPIO_LED_WPS, 0);

	ath79_gpio_output_select(CF_WR630AC_GPIO_LED_WAN1, 0);
	ath79_gpio_output_select(CF_WR630AC_GPIO_LED_LAN1, 0);
	ath79_gpio_output_select(CF_WR630AC_GPIO_LED_LAN2, 0);
	ath79_gpio_output_select(CF_WR630AC_GPIO_LED_LAN3, 0);
	ath79_gpio_output_select(CF_WR630AC_GPIO_LED_LAN4, 0);

	ath79_register_m25p80(NULL);

	ath79_setup_ar933x_phy4_switch(false, false);

	ath79_register_mdio(0, 0x0);

	ath79_init_mac(ath79_eth0_data.mac_addr, mac, 0);
	ath79_init_mac(ath79_eth1_data.mac_addr, mac, 1);

	// wan
	ath79_switch_data.phy_poll_mask |= BIT(4);
	ath79_switch_data.phy4_mii_en = 1;
	ath79_eth0_data.speed = SPEED_100;
	ath79_eth0_data.duplex = DUPLEX_FULL;
	ath79_eth0_data.phy_mask = BIT(4);
	ath79_eth0_data.phy_if_mode = PHY_INTERFACE_MODE_MII;
	ath79_register_eth(0);

	// lan
	ath79_eth1_data.phy_if_mode = PHY_INTERFACE_MODE_GMII;
	ath79_register_eth(1);

	ath79_register_wmac(art + WMAC_CALDATA_OFFSET, wlan0_mac);
	ap91_pci_init(art + PCIE_CALDATA_OFFSET, wlan1_mac);
}

static void __init cf_wr630ac_setup(void)
{
	comfast_wr630ac_setup();

	ath79_register_leds_gpio(-1, ARRAY_SIZE(cf_wr630ac_leds_gpio),
			cf_wr630ac_leds_gpio);

	ath79_register_gpio_keys_polled(1, KEYS_POLL_INTERVAL,
			ARRAY_SIZE(cf_wr630ac_gpio_keys),
			cf_wr630ac_gpio_keys);

	ath79_register_usb();
}
MIPS_MACHINE(ATH79_MACH_COMFAST_CF_WR630AC, "CF-WR630AC", "COMFAST CF-WR630AC", cf_wr630ac_setup);
MIPS_MACHINE(ATH79_MACH_COMFAST_CF_WR635AC, "CF-WR635AC", "COMFAST CF-WR635AC", cf_wr630ac_setup);
MIPS_MACHINE(ATH79_MACH_CAIHUO_Z1, "CAIHUO-Z1", "CaiHuo Z1", cf_wr630ac_setup);

