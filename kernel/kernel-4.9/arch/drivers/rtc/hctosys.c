/*
 * RTC subsystem, initialize system time on startup
 *
 * Copyright (C) 2016-2017, NVIDIA CORPORATION. All rights reserved.
 * Copyright (C) 2005 Tower Technologies
 * Author: Alessandro Zummo <a.zummo@towertech.it>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/rtc.h>

/* IMPORTANT: the RTC only stores whole seconds. It is arbitrary
 * whether it stores the most close value or the value with partial
 * seconds truncated. However, it is important that we use it to store
 * the truncated value. This is because otherwise it is necessary,
 * in an rtc sync function, to read both xtime.tv_sec and
 * xtime.tv_nsec. On some processors (i.e. ARM), an atomic read
 * of >32bits is not possible. So storing the most close value would
 * slow down the sync API. So here we have the truncated value and
 * the best guess is to add 0.5s.
 */


static int set_hctosys_rtc_time(struct rtc_device *rtc)
{
	int err = -ENODEV;
	struct rtc_time tm;
	struct rtc_device *backup_rtc;

	backup_rtc = rtc_class_open(CONFIG_RTC_BACKUP_HCTOSYS_DEVICE);
	if (backup_rtc == NULL)
		return err;

	err = rtc_read_time(backup_rtc, &tm);
	if (err) {
		rtc_class_close(backup_rtc);
		return err;
	}

	err = rtc_set_time(rtc, &tm);
	if (err) {
		rtc_class_close(backup_rtc);
		return err;
	}

	rtc_class_close(backup_rtc);
	return 0;
}

int set_systohc_rtc_time(void)
{
	int err = -ENODEV;
	struct rtc_time tm;
	struct rtc_device *backup_rtc;
	struct rtc_device *system_rtc;

	system_rtc = rtc_class_open(CONFIG_RTC_HCTOSYS_DEVICE);
	if (system_rtc == NULL)
		return err;

	err = rtc_read_time(system_rtc, &tm);
	if (err) {
		rtc_class_close(system_rtc);
		return err;
	}

	backup_rtc = rtc_class_open(CONFIG_RTC_BACKUP_HCTOSYS_DEVICE);
	if (backup_rtc == NULL) {
		rtc_class_close(system_rtc);
		return err;
	}

	err = rtc_set_time(backup_rtc, &tm);
	if (err) {
		rtc_class_close(backup_rtc);
		return err;
	}

	rtc_class_close(system_rtc);
	rtc_class_close(backup_rtc);

	return 0;
}
EXPORT_SYMBOL(set_systohc_rtc_time);

static int __init rtc_hctosys(void)
{
	int err = -ENODEV;
	struct rtc_time tm;
	struct timespec64 tv64 = {
		.tv_nsec = NSEC_PER_SEC >> 1,
	};
	struct rtc_device *rtc = rtc_class_open(CONFIG_RTC_HCTOSYS_DEVICE);

	if (rtc == NULL) {
		pr_info("unable to open rtc device (%s)\n",
			CONFIG_RTC_HCTOSYS_DEVICE);
		goto err_open;
	}

	if (CONFIG_RTC_BACKUP_HCTOSYS_DEVICE[0] != '\0') {
		err = set_hctosys_rtc_time(rtc);
		if (err)
			pr_warn("%s: Ignoring backup rtc device (%s)\n",
				__FILE__, CONFIG_RTC_BACKUP_HCTOSYS_DEVICE);
	}

	err = rtc_read_time(rtc, &tm);
	if (err) {
		dev_err(rtc->dev.parent,
			"hctosys: unable to read the hardware clock\n");
		goto err_read;

	}

	tv64.tv_sec = rtc_tm_to_time64(&tm);

#if BITS_PER_LONG == 32
	if (tv64.tv_sec > INT_MAX) {
		err = -ERANGE;
		goto err_read;
	}
#endif

	err = do_settimeofday64(&tv64);

	dev_info(rtc->dev.parent,
		"setting system clock to "
		"%d-%02d-%02d %02d:%02d:%02d UTC (%lld)\n",
		tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
		tm.tm_hour, tm.tm_min, tm.tm_sec,
		(long long) tv64.tv_sec);

err_read:
	rtc_class_close(rtc);

err_open:
	rtc_hctosys_ret = err;

	return err;
}

late_initcall(rtc_hctosys);
