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

/* ARM-side mailbox polling thread: calls URPC_Poll() which checks
 * CP_MAILBOX_IRQ and feeds URPC client pipeline when data arrives. */
static void mbox_poll_thread(void *arg1, void *arg2, void *arg3)
{
	extern int8_t URPC_Poll(void);
	while (1) {
		URPC_Poll();
		k_sleep(K_MSEC(10));
	}
}

K_THREAD_DEFINE(mbox_poll, 2048, mbox_poll_thread, NULL, NULL, NULL,
		K_PRIO_PREEMPT(5), 0, 0);

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
	printk("LSF Initialized. Connecting to DSP...\n");

	lsf_connect();
	printk("[ARM] Connected to DSP!\n");

	ICFenceHandle fence = IC_Proxy_getRemoteFence(0);
	printk("LSF Connected. Waiting for remote fence...\n");

	ICFence_syncWithRemote(fence);
	printk("DSP Synced. Waiting for DSP ready...\n");

	uint32_t val;
	ICFence_wait(fence, &val);
	printk("DSP Ready! Fence value: 0x%08x\n", val);

	STRUCT_SECTION_FOREACH(lsf_service, service) {
		printk("Initializing service %s\n", service->name);
		ret = service->init();
		if (ret != 0) {
			printk("ERROR: Failed to init service %s: %d\n", service->name, ret);
			return ret;
		}
	}
	printk("All services initialized\n");
	inited = true;

	printk("Monitoring DSP diag...\n");
	while (1) {
		k_sleep(K_MSEC(1000));
		dcache_invalidate_range(0x30700000, 0x30700020);
		uint32_t d  = *(volatile uint32_t *)0x3070000C;
		uint32_t d2 = *(volatile uint32_t *)0x30700010;
		uint32_t mbox = *(volatile uint32_t *)0x46100028;
		printk("[DSP] pr=0x%08x mc=0x%08x ir=0x%08x\n", d, d2, mbox);
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
