/*
 * Copyright(c) 2023 LISTENAI
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT lsf_service_controller

#include <zephyr/device.h>
#include <zephyr/kernel.h>

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(lsf, LOG_LEVEL_DBG);

#include <lsf.h>
#include <ic_proxy.h>
#include <ic_fence.h>

#include <controller.h>
#include <service.h>
#include "cache.h"

static volatile bool inited = false;

int lsf_controller_init(void)
{
	int ret;

	if (inited) {
		return 0;
	}

	printk("=== LSF Controller Init Start ===\n");
	printk("Checking DSP boot status (0x30700000): 0x%08x\n", *(volatile uint32_t *)0x30700000);
	printk("Checking DSP magic (0x30700004): 0x%08x\n", *(volatile uint32_t *)0x30700004);
	printk("Checking DSP diag (0x3070000C): 0x%08x\n", *(volatile uint32_t *)0x3070000C);
	printk("Checking DSP check2 (0x30700018): 0x%08x\n", *(volatile uint32_t *)0x30700018);

	lsf_init();
	printk("LSF Initialized. Testing mailbox ping...\n");

	/* Send a single test PING to DSP via mailbox */
	*(volatile uint32_t *)0x46100010 = 0xDEAD0001;
	*(volatile uint32_t *)0x46100014 = 0xDEAD0002;
	*(volatile uint32_t *)0x46100018 = 0xDEAD0003;
	*(volatile uint32_t *)0x4610001C = 0xDEAD0004;
	__DSB();
	*(volatile uint32_t *)0x46100004 |= (1 << 16);
	__DSB();

	printk("PING sent. Monitoring DSP diag...\n");

	while (1) {
		k_sleep(K_MSEC(1000));
		dcache_invalidate_range(0x30700000, 0x30700020);
		uint32_t d  = *(volatile uint32_t *)0x3070000C;
		uint32_t d2 = *(volatile uint32_t *)0x30700010;
		uint32_t d3 = *(volatile uint32_t *)0x30700014;
		uint32_t mbox_irq = *(volatile uint32_t *)0x46100028;
		printk("[DSP] pr=0x%08x mc=0x%08x mb=0x%08x ir=0x%08x\n", d, d2, d3, mbox_irq);
	}
}

static int lsf_controller_init_internal(const struct device *dev)
{
	ARG_UNUSED(dev);

#if DT_HAS_CHOSEN(lsf_dsp_firmware)
	return lsf_controller_init();
#else  /* DT_HAS_CHOSEN(lsf_dsp_firmware) */
	return 0;
#endif /* DT_HAS_CHOSEN(lsf_dsp_firmware) */
}

DEVICE_DT_INST_DEFINE(0, lsf_controller_init_internal, NULL, NULL, NULL, APPLICATION,
		      CONFIG_APPLICATION_INIT_PRIORITY, NULL);
