/*
 * Copyright (c) Samsung Electronics Co., Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/lcd.h>
#include "../dsim.h"

#include "dsim_panel.h"

#if IS_ENABLED(CONFIG_EXYNOS_DECON_LCD_S6E3HA2K)
struct dsim_lcd_driver *mipi_lcd_driver = &s6e3ha2k_mipi_lcd_driver;
#elif IS_ENABLED(CONFIG_EXYNOS_DECON_LCD_S6E3HF4)
struct dsim_lcd_driver *mipi_lcd_driver = &s6e3hf4_mipi_lcd_driver;
#elif IS_ENABLED(CONFIG_EXYNOS_DECON_LCD_S6E3HA6)
struct dsim_lcd_driver *mipi_lcd_driver = &s6e3ha6_mipi_lcd_driver;
#elif IS_ENABLED(CONFIG_EXYNOS_DECON_LCD_S6E3HA8)
struct dsim_lcd_driver *mipi_lcd_driver = &s6e3ha8_mipi_lcd_driver;
#elif IS_ENABLED(CONFIG_EXYNOS_DECON_LCD_S6E3AA2)
struct dsim_lcd_driver *mipi_lcd_driver = &s6e3aa2_mipi_lcd_driver;
#elif IS_ENABLED(CONFIG_EXYNOS_DECON_LCD_S6E3FA0)
struct dsim_lcd_driver *mipi_lcd_driver = &s6e3fa0_mipi_lcd_driver;
#elif IS_ENABLED(CONFIG_EXYNOS_DECON_LCD_S6E3FA7)
struct dsim_lcd_driver *mipi_lcd_driver = &s6e3fa7_mipi_lcd_driver;
#elif IS_ENABLED(CONFIG_EXYNOS_DECON_LCD_EMUL_DISP)
struct dsim_lcd_driver *mipi_lcd_driver = &emul_disp_mipi_lcd_driver;
#elif IS_ENABLED(CONFIG_EXYNOS_DECON_LCD_S6E3FA7_A50)
struct dsim_lcd_driver *mipi_lcd_driver = &s6e3fa7_mipi_lcd_driver;
#elif IS_ENABLED(CONFIG_EXYNOS_DECON_LCD_EA8076_A50)
struct dsim_lcd_driver *mipi_lcd_driver = &ea8076_mipi_lcd_driver;
#elif IS_ENABLED(CONFIG_EXYNOS_DECON_LCD_S6E3FC2_A80)
struct dsim_lcd_driver *mipi_lcd_driver = &s6e3fc2_mipi_lcd_driver;
#else
struct dsim_lcd_driver *mipi_lcd_driver = &s6e3ha2k_mipi_lcd_driver;
#endif

void dsim_register_panel(struct dsim_device *dsim)
{
	dsim->panel_ops = mipi_lcd_driver;
}

int register_lcd_driver(struct dsim_lcd_driver *drv)
{
	struct device_node *node;
	int count = 0;
	char *dts_name = "lcd_info";

	node = of_find_node_with_property(NULL, dts_name);
	if (!node) {
		dsim_info("%s: of_find_node_with_property\n", __func__);
		goto exit;
	}

	count = of_count_phandle_with_args(node, dts_name, NULL);
	if (!count) {
		dsim_info("%s: of_count_phandle_with_args\n", __func__);
		goto exit;
	}

	node = of_parse_phandle(node, dts_name, 0);
	if (!node) {
		dsim_info("%s: of_parse_phandle\n", __func__);
		goto exit;
	}

	if (count != 1) {
		dsim_info("%s: we need only one phandle in lcd_info\n", __func__);
		goto exit;
	}

	if (IS_ERR_OR_NULL(drv) || IS_ERR_OR_NULL(drv->name)) {
		dsim_info("%s: we need lcd_drv name to compare with device tree name(%s)\n", __func__, node->name);
		goto exit;
	}

	if (strstarts(node->name, drv->name)) {
		mipi_lcd_driver = drv;
		dsim_info("%s: %s is registered\n", __func__, mipi_lcd_driver->name);
	} else
		dsim_info("%s: %s is not with prefix: %s\n", __func__, node->name, drv->name);

exit:
	return 0;
}


unsigned int lcdtype;
EXPORT_SYMBOL(lcdtype);

static int __init get_lcd_type(char *arg)
{
	get_option(&arg, &lcdtype);

	dsim_info("%s: lcdtype: %8X\n", __func__, lcdtype);

	return 0;
}
early_param("lcdtype", get_lcd_type);

