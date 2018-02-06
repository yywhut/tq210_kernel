/*
 * Copyright (c) 2002-3 Patrick Mochel
 * Copyright (c) 2002-3 Open Source Development Labs
 *
 * This file is released under the GPLv2
 */

#include <linux/device.h>
#include <linux/init.h>
#include <linux/memory.h>

#include "base.h"

/**
 * driver_init - initialize driver model.
 *
 * Call the driver model init functions to initialize their
 * subsystems. Called early from init/main.c.
 */

// 这里是最早的一层
void __init driver_init(void)
{
	/* These are the core pieces */
	devtmpfs_init();
	devices_init();  // 这里初始化了/sys/devices 这个目录 
	buses_init();   //  这里建立了 /sys/bus目录
	classes_init();  // 这里建立了/sys/class
	firmware_init();
	hypervisor_init();

	/* These are also core pieces, but must come after the
	 * core core pieces.
	 */
	platform_bus_init();// sys/devices/platform
	system_bus_init();  //   /sys/system
	cpu_dev_init();
	memory_dev_init();
}
