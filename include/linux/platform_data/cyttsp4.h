/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Header file for:
 * Cypress TrueTouch(TM) Standard Product (TTSP) touchscreen drivers.
 * For use with Cypress Txx3xx parts.
 * Supported parts include:
 * CY8CTST341
 * CY8CTMA340
 *
 * Copyright (C) 2009, 2010, 2011 Cypress Semiconductor, Inc.
 * Copyright (C) 2012 Javier Martinez Canillas <javier@dowhile0.org>
 *
 * Contact Cypress Semiconductor at www.cypress.com (kev@cypress.com)
 */
#ifndef _CYTTSP4_H_
#define _CYTTSP4_H_

#define CYTTSP4_MT_NAME "cyttsp4_mt"
#define CYTTSP4_I2C_NAME "cyttsp4_i2c_adapter"
#define CYTTSP4_SPI_NAME "cyttsp4_spi_adapter"

#define CY_TOUCH_SETTINGS_MAX 32

/* abs axis signal offsets in the signals array  */
enum cyttsp4_sig_ost {
	CY_ABS_X_OST,
	CY_ABS_Y_OST,
	CY_ABS_P_OST,
	CY_ABS_W_OST,
	CY_ABS_ID_OST,
	CY_ABS_MAJ_OST,
	CY_ABS_MIN_OST,
	CY_ABS_OR_OST,
	CY_NUM_ABS_OST	/* number of abs signals */
};

struct cyttsp4_virtual_key {
	int code;
};

struct cyttsp4_signal_def {
	int signal;
	int min;
	int max;
	int fuzz;
	int flat;
};

struct cyttsp4_platform_data {
	char const *inp_dev_name;
	unsigned short flags;

	int irq_gpio;
	int rst_gpio;
	int level_irq_udelay;
	int (*xres)(struct cyttsp4_platform_data *pdata,
		struct device *dev);
	int (*init)(struct cyttsp4_platform_data *pdata,
		int on, struct device *dev);
	int (*power)(struct cyttsp4_platform_data *pdata,
		int on, struct device *dev, atomic_t *ignore_irq);
	int (*irq_stat)(struct cyttsp4_platform_data *pdata,
		struct device *dev);

	int n_signals;
	struct cyttsp4_signal_def *signals;

	int n_keys;
	struct cyttsp4_virtual_key* keys;
};

#endif /* _CYTTSP4_H_ */
