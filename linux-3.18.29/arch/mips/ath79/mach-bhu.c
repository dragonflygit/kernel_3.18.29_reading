/*
 *  BHU board support
 *
 *  Copyright (C) 2013-2014 Terry Yang <yan...@bhunetworks.com>
 *
 *  This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published
 *  by the Free Software Foundation.
 */

#include <linux/gpio.h>
#include <linux/platform_device.h>

#include <asm/mach-ath79/ath79.h>
#include <asm/mach-ath79/ar71xx_regs.h>

#include "common.h"
#include "dev-eth.h"
#include "dev-gpio-buttons.h"
#include "dev-leds-gpio.h"
#include "dev-m25p80.h"
#include "dev-usb.h"
#include "dev-wmac.h"
#include "machtypes.h"

/*
 * BHU BXU2000n-2 A1 board
 */

#define BHU_BXU2000N2_A1_GPIO_LED_WLAN        13
#define BHU_BXU2000N2_A1_GPIO_LED_WAN        19
#define BHU_BXU2000N2_A1_GPIO_LED_LAN        21
#define BHU_BXU2000N2_A1_GPIO_LED_SYSTEM    14

#define BHU_BXU2000N2_A1_GPIO_BTN_RESET        17

#define BHU_BXU2000N2_KEYS_POLL_INTERVAL    20    /* msecs */
#define BHU_BXU2000N2_KEYS_DEBOUNCE_INTERVAL    \
    (3 * BHU_BXU2000N2_KEYS_POLL_INTERVAL)

static const char *bhu_bxu2000n2_part_probes[] = {
    "cmdlinepart",
    NULL,
};

static struct flash_platform_data bhu_bxu2000n2_flash_data = {
    .part_probes    = bhu_bxu2000n2_part_probes,
};

static struct gpio_led bhu_bxu2000n2_a1_leds_gpio[] __initdata = {
    {
        .name        = "bhu:green:status",
        .gpio        = BHU_BXU2000N2_A1_GPIO_LED_SYSTEM,
        .active_low    = 1,
    }, {
        .name        = "bhu:green:lan",
        .gpio        = BHU_BXU2000N2_A1_GPIO_LED_LAN,
        .active_low    = 1,
    }, {
        .name        = "bhu:green:wan",
        .gpio        = BHU_BXU2000N2_A1_GPIO_LED_WAN,
        .active_low    = 1,
    }, {
        .name        = "bhu:green:wlan",
        .gpio        = BHU_BXU2000N2_A1_GPIO_LED_WLAN,
        .active_low    = 1,
    },
};

static struct gpio_keys_button bhu_bxu2000n2_a1_gpio_keys[] __initdata = {
    {
        .desc        = "Reset button",
        .type        = EV_KEY,
        .code        = KEY_RESTART,
        .debounce_interval = BHU_BXU2000N2_KEYS_DEBOUNCE_INTERVAL,
        .gpio        = BHU_BXU2000N2_A1_GPIO_BTN_RESET,
        .active_low    = 1,
    }
};

static void __init bhu_ap123_setup(void)
{
    u8 *mac = (u8 *) KSEG1ADDR(0x1fff0000);
    u8 *ee = (u8 *) KSEG1ADDR(0x1fff1000);

    ath79_register_m25p80(&bhu_bxu2000n2_flash_data);

    ath79_register_mdio(1, 0x0);

    ath79_init_mac(ath79_eth0_data.mac_addr, mac, 0);
    ath79_init_mac(ath79_eth1_data.mac_addr, mac, 1);

    /* GMAC0 is connected to the PHY4 of the internal switch */
    ath79_switch_data.phy4_mii_en = 1;
    ath79_switch_data.phy_poll_mask = BIT(4);
    ath79_eth0_data.phy_if_mode = PHY_INTERFACE_MODE_MII;
    ath79_eth0_data.phy_mask = BIT(4);
    ath79_eth0_data.mii_bus_dev = &ath79_mdio1_device.dev;
    ath79_register_eth(0);

    /* GMAC1 is connected to the internal switch. Only use PHY3 */
    ath79_eth1_data.phy_if_mode = PHY_INTERFACE_MODE_GMII;
    ath79_eth1_data.phy_mask = BIT(3);
    ath79_register_eth(1);

    ath79_register_wmac(ee, ee+2);
}

static void __init bhu_bxu2000n2_a1_setup(void)
{
    bhu_ap123_setup();

    ath79_register_leds_gpio(-1, ARRAY_SIZE(bhu_bxu2000n2_a1_leds_gpio),
                 bhu_bxu2000n2_a1_leds_gpio);

    ath79_register_gpio_keys_polled(1, BHU_BXU2000N2_KEYS_POLL_INTERVAL,
                    ARRAY_SIZE(bhu_bxu2000n2_a1_gpio_keys),
                    bhu_bxu2000n2_a1_gpio_keys);
}

MIPS_MACHINE(ATH79_MACH_BHU_BXU2000N2_A1, "BXU2000n-2-A1",
         "BHU BXU2000n-2 rev. A1",
         bhu_bxu2000n2_a1_setup);

/*
 * BHU BXO2000n-2S A1 board
 */

#define BHU_BXO2000N2S_A1_GPIO_LED_SYSTEM   14
#define BHU_BXO2000N2S_A1_GPIO_LED_S0       14
#define BHU_BXO2000N2S_A1_GPIO_LED_S1       19
#define BHU_BXO2000N2S_A1_GPIO_LED_S2       15
#define BHU_BXO2000N2S_A1_GPIO_LED_S3       21
#define BHU_BXO2000N2S_A1_GPIO_LED_WLAN     13
#define BHU_BXO2000N2S_A1_GPIO_LED_WAN      20
#define BHU_BXO2000N2S_A1_GPIO_LED_LAN      22

#define BHU_BXO2000N2S_A1_GPIO_BTN_RESET        17

#define BHU_BXO2000N2S_KEYS_POLL_INTERVAL   20  /* msecs */
#define BHU_BXO2000N2S_KEYS_DEBOUNCE_INTERVAL   \
    (3 * BHU_BXO2000N2S_KEYS_POLL_INTERVAL)

static struct gpio_led bhu_bxo2000n2s_a1_leds_gpio[] __initdata = {
    {
        .name       = "bhu:green:status",
        .gpio       = BHU_BXO2000N2S_A1_GPIO_LED_SYSTEM,
        .active_low = 1,
    }, {
        .name       = "bhu:green:lan",
        .gpio       = BHU_BXO2000N2S_A1_GPIO_LED_LAN,
        .active_low = 1,
    }, {
        .name       = "bhu:green:wan",
        .gpio       = BHU_BXO2000N2S_A1_GPIO_LED_WAN,
        .active_low = 1,
    }, {
        .name       = "bhu:green:wlan",
        .gpio       = BHU_BXO2000N2S_A1_GPIO_LED_WLAN,
        .active_low = 1,
    }, {
        .name       = "bhu:green:rssilow",
        .gpio       = BHU_BXO2000N2S_A1_GPIO_LED_S1,
        .active_low = 1,
    }, {
        .name       = "bhu:green:rssimedium",
        .gpio       = BHU_BXO2000N2S_A1_GPIO_LED_S2,
        .active_low = 1,
    }, {
        .name       = "bhu:green:rssihigh",
        .gpio       = BHU_BXO2000N2S_A1_GPIO_LED_S3,
        .active_low = 1,
    },
};

static struct gpio_keys_button bhu_bxo2000n2s_a1_gpio_keys[] __initdata = {
    {
        .desc       = "Reset button",
        .type       = EV_KEY,
        .code       = KEY_RESTART,
        .debounce_interval = BHU_BXO2000N2S_KEYS_DEBOUNCE_INTERVAL,
        .gpio       = BHU_BXO2000N2S_A1_GPIO_BTN_RESET,
        .active_low = 1,
    }
};

static void __init bhu_bxo2000n2s_a1_setup(void)
{
    bhu_ap123_setup();

    ath79_register_leds_gpio(-1, ARRAY_SIZE(bhu_bxo2000n2s_a1_leds_gpio),
                 bhu_bxo2000n2s_a1_leds_gpio);

    ath79_register_gpio_keys_polled(1, BHU_BXO2000N2S_KEYS_POLL_INTERVAL,
                    ARRAY_SIZE(bhu_bxo2000n2s_a1_gpio_keys),
                    bhu_bxo2000n2s_a1_gpio_keys);
}

MIPS_MACHINE(ATH79_MACH_BHU_BXO2000N2S_A1, "BXO2000n-2S-A1",
         "BHU BXO2000n-2S rev. A1",
         bhu_bxo2000n2s_a1_setup);
/*
 * BHU HQ61 board
 */

#define BHU_HQ61_GPIO_LED_WLAN     13
#define BHU_HQ61_GPIO_LED_LAN      21
#define BHU_HQ61_GPIO_LED_WAN      22

#define BHU_HQ61_GPIO_BTN_RESET        17

#define BHU_HQ61_KEYS_POLL_INTERVAL   20  /* msecs */
#define BHU_HQ61_KEYS_DEBOUNCE_INTERVAL   \
    (3 * BHU_HQ61_KEYS_POLL_INTERVAL)

static struct gpio_led bhu_hq61_leds_gpio[] __initdata = {
    {
        .name       = "bhu:green:lan",
        .gpio       = BHU_HQ61_GPIO_LED_LAN,
        .active_low = 1,
    }, {
        .name       = "bhu:green:wan",
        .gpio       = BHU_HQ61_GPIO_LED_WAN,
        .active_low = 1,
    }, {
        .name       = "bhu:green:wlan",
        .gpio       = BHU_HQ61_GPIO_LED_WLAN,
        .active_low = 1,
    },
};

static struct gpio_keys_button bhu_hq61_gpio_keys[] __initdata = {
    {
        .desc       = "Reset button",
        .type       = EV_KEY,
        .code       = KEY_RESTART,
        .debounce_interval = BHU_HQ61_KEYS_DEBOUNCE_INTERVAL,
        .gpio       = BHU_HQ61_GPIO_BTN_RESET,
        .active_low = 1,
    }
};

static void __init bhu_hq61_setup(void)
{
    bhu_ap123_setup();

    ath79_register_leds_gpio(-1, ARRAY_SIZE(bhu_hq61_leds_gpio),
                 bhu_hq61_leds_gpio);

    ath79_register_gpio_keys_polled(1, BHU_HQ61_KEYS_POLL_INTERVAL,
                    ARRAY_SIZE(bhu_hq61_gpio_keys),
                    bhu_hq61_gpio_keys);
}

MIPS_MACHINE(ATH79_MACH_BHU_HQ61, "BHU-HQ61",
         "BHU HQ61",
         bhu_hq61_setup);

