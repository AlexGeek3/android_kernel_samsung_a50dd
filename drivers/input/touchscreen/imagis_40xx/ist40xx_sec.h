/*
 *  Copyright (C) 2010, Imagis Technology Co. Ltd. All Rights Reserved.
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 */

#ifndef __IST40XX_SEC_H__
#define __IST40XX_SEC_H__

#define SEC_MISCAL_SPEC		50

/* Factory Test for Reliability Test Group */
enum ist40xx_reliability_commands {
	TEST_CDC_ALL_DATA = 0,
	TEST_CM_ALL_DATA,
	TEST_CS_ALL_DATA,
	TEST_SLOPE0_ALL_DATA,
	TEST_SLOPE1_ALL_DATA,
};

/* AS Tool Origin */
enum ist40xx_origins {
	BTT_RTL = 0,
	BTT_LTR,
	TTB_RTL,
	TTB_LTR,
	LTR_TTB,
	LTR_BTT,
	RTL_TTB,
	RTL_BTT,
	ORI_MAX,
};

/*
 * support_feature
 * bit value should be made a promise with InputFramework.
 */
#define INPUT_FEATURE_ENABLE_SETTINGS_AOT	(1 << 0) /* Double tap wakeup settings */

#endif  // __IST40XX_SEC_H__
