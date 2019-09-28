/*
* Samsung Exynos5 SoC series FIMC-IS driver
 *
 * exynos5 fimc-is vender functions
 *
 * Copyright (c) 2015 Samsung Electronics Co., Ltd
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef FIMC_IS_VENDOR_CONFIG_H
#define FIMC_IS_VENDOR_CONFIG_H

#define USE_BINARY_PADDING_DATA_ADDED             /* Apply Sign DDK/RTA Binary */

#if defined(USE_BINARY_PADDING_DATA_ADDED) && (defined(CONFIG_FIMC_IS_DDK_DATA_LOAD) || defined(CONFIG_SAMSUNG_PRODUCT_SHIP))
#define USE_TZ_CONTROLLED_MEM_ATTRIBUTE
#endif

#if defined(CONFIG_CAMERA_VOGUE)
#include "fimc-is-vendor-config_vogue.h"
#elif defined(CONFIG_CAMERA_A50)
#include "fimc-is-vendor-config_a50.h"
#else
#include "fimc-is-vendor-config_a50.h" /* Default */
#endif

#endif
