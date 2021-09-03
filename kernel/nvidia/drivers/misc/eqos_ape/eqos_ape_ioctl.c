/*
 * eqos_ape_ioctl.c -- EQOS APE Clock Synchronization driver IO control
 *
 * Copyright (c) 2015-2021 NVIDIA CORPORATION.  All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */
#include <linux/clk.h>
#include <linux/module.h>
#include <linux/err.h>
#include <linux/slab.h>
#include <linux/io.h>
#include <linux/pm_runtime.h>
#include <linux/tegra_pm_domains.h>
#include <linux/clk/tegra.h>
#include <linux/uaccess.h>
#include <linux/cdev.h>
#include <linux/of.h>
#include <uapi/misc/eqos_ape_ioctl.h>
#include "eqos_ape_global.h"

#define ONE_MILLION		(1000000)
#define ONE_BILLION		(1000000000)
#define DEFAULT_N_INT		(0)
#define DEFAULT_N_FRACT		(0)
#define DEFAULT_N_MODULO	(0)

static struct rate_to_time_period g_rate;
static int eqos_ape_ioctl_major;
static struct cdev eqos_ape_ioctl_cdev;
static struct device *dev_eqos_ape;
static struct class *eqos_ape_ioctl_class;
struct eqos_drvdata *eqos_ape_drv_data;

static void sync_snapshot(void)
{
	struct eqos_drvdata *data = eqos_ape_drv_data;

	/* trigger and store snapsnot */
	amisc_writel((AMISC_APE_TSC_CTRL_3_0_ENABLE |
				AMISC_APE_TSC_CTRL_3_0_TRIGGER),
				AMISC_APE_TSC_CTRL_3_0);
	data->ape_sec_snap = amisc_readl(AMISC_APE_SNAP_TSC_SEC_0);
	data->ape_ns_snap = amisc_readl(AMISC_APE_SNAP_TSC_NS_0);
	data->eavb_sec_snap = amisc_readl(AMISC_EAVB_SNAP_TSC_SEC_0);
	data->eavb_ns_snap = amisc_readl(AMISC_EAVB_SNAP_TSC_NS_0);
}

static long eqos_ape_hw_ioctl(struct file *file,
unsigned int cmd, unsigned long arg)
{
	struct eqos_ape_cmd __user *_eqos_ape = (struct eqos_ape_cmd *)arg;
	struct eqos_ape_cmd eqos_ape;
	struct eqos_ape_sync_cmd __user *_eqos_ape_sync =
					(struct eqos_ape_sync_cmd *)arg;
	struct eqos_ape_sync_cmd eqos_ape_sync;
	struct rate_to_time_period __user *_rate_info =
					(struct rate_to_time_period *)arg;
	struct rate_to_time_period rate_info = {0, 0, 0, 0};
	u64 ape_sec_snap_prev = 0, ape_ns_snap_prev = 0;
	u64 eavb_sec_snap_prev = 0, eavb_ns_snap_prev = 0;
	struct eqos_drvdata *data = eqos_ape_drv_data;
	struct device *dev = &data->pdev->dev;
	u64 num = 0;
	u64 den = 0;
	int cur_rate;
	int new_rate;
	int set_rate;

	switch (cmd) {
	case EQOS_APE_AMISC_INIT:
		amisc_idle_disable();
		amisc_writel(AMISC_APE_TSC_CTRL_3_0_ENABLE,
					AMISC_APE_TSC_CTRL_3_0);

		if (!(arg && copy_from_user(&rate_info,
					_rate_info,
					sizeof(rate_info)))) {
			g_rate.n_modulo = rate_info.n_modulo;
			g_rate.n_fract = rate_info.n_fract;
			g_rate.n_int = rate_info.n_int;
			amisc_writel(
				(AMISC_APE_TSC_CTRL_NMODULE_0_0_MASK
							(rate_info.n_modulo) |
				AMISC_APE_TSC_CTRL_NFRACT_0_0_MASK
							(rate_info.n_fract)),
				AMISC_APE_TSC_CTRL_0_0);
			amisc_writel(AMISC_APE_TSC_CTRL_NINT_1_0_MASK
							(rate_info.n_int),
				AMISC_APE_TSC_CTRL_1_0);
			amisc_writel((AMISC_APE_TSC_CTRL_3_0_ENABLE |
				AMISC_APE_TSC_CTRL_3_0_COPY),
				AMISC_APE_TSC_CTRL_3_0);

			ape_sec_snap_prev = amisc_readl(AMISC_APE_RT_TSC_SEC_0);
			ape_ns_snap_prev = amisc_readl(AMISC_APE_RT_TSC_NS_0);
			dev_dbg(dev, "APE Time Sec %lld NSec %lld\n",
				ape_sec_snap_prev, ape_ns_snap_prev);
			dev_dbg(dev, "APE Time Sec %llx NSec %llx\n",
				ape_sec_snap_prev, ape_ns_snap_prev);
		} else {
			return -EFAULT;
		}
		break;

	case EQOS_APE_AMISC_FREQ_SYNC:
		if (data->first_sync) {
			sync_snapshot();
			data->first_sync = 0;
			dev_dbg(dev, "EAVB sec %lld nsec %lld\n",
				data->eavb_sec_snap, data->eavb_ns_snap);
			dev_dbg(dev, "APE sec %lld nsec %lld\n",
				data->ape_sec_snap, data->ape_ns_snap);
			break;
		}
		if (arg && copy_from_user(&eqos_ape_sync,
					_eqos_ape_sync,
					sizeof(eqos_ape_sync)))
			return -EFAULT;



		/* Store previous timestamp*/
		eavb_sec_snap_prev = data->eavb_sec_snap;
		eavb_ns_snap_prev = data->eavb_ns_snap;
		ape_sec_snap_prev = data->ape_sec_snap;
		ape_ns_snap_prev = data->ape_ns_snap;

		sync_snapshot();
		/* TODO: implementation for clock change logic */
		den = (data->ape_sec_snap - ape_sec_snap_prev) * ONE_BILLION
		+ (data->ape_ns_snap - ape_ns_snap_prev);
		num = (data->eavb_sec_snap - eavb_sec_snap_prev) * ONE_BILLION
		+ (data->eavb_ns_snap - eavb_ns_snap_prev);

		eqos_ape_sync.drift_num = num;
		eqos_ape_sync.drift_den = den;

		dev_dbg(dev, "num %lld den %lld\n", num, den);
		if (copy_to_user((struct eqos_ape_sync_cmd *)_eqos_ape_sync,
					&eqos_ape_sync,
					sizeof(eqos_ape_sync)))
			return -EFAULT;

		break;
	case EQOS_APE_TEST_FREQ_ADJ:

		if (!arg || copy_from_user(&eqos_ape,
			_eqos_ape,
			sizeof(eqos_ape)))
			return -EFAULT;

		dev_dbg(dev, "Applied freq adj %d\n", eqos_ape.ppm);
		cur_rate = amisc_plla_get_rate();
		dev_dbg(dev, "current rate %d\n", cur_rate);

		num = ONE_MILLION + eqos_ape.ppm;
		den = ONE_MILLION;

		new_rate = (int)((num * cur_rate)/den);

		dev_dbg(dev, "new rate %d\n", new_rate);
		amisc_plla_set_rate(new_rate);

		set_rate = amisc_plla_get_rate();
		dev_dbg(dev, "applied rate %d\n", set_rate);

		break;
	case EQOS_APE_AMISC_PHASE_SYNC:

		amisc_writel((AMISC_APE_TSC_CTRL_3_0_ENABLE |
			AMISC_APE_TSC_CTRL_3_0_COPY),
			AMISC_APE_TSC_CTRL_3_0);
		data->first_sync = 1;
		break;

	case EQOS_APE_AMISC_DEINIT:
		amisc_writel((AMISC_APE_TSC_CTRL_NMODULE_0_0_MASK(0) |
			AMISC_APE_TSC_CTRL_NFRACT_0_0_MASK(0)),
			AMISC_APE_TSC_CTRL_0_0);
		amisc_writel(AMISC_APE_TSC_CTRL_NINT_1_0_MASK(DEFAULT_N_INT),
			AMISC_APE_TSC_CTRL_1_0);
		amisc_writel(AMISC_APE_TSC_CTRL_3_0_DISABLE,
			AMISC_APE_TSC_CTRL_3_0);
		amisc_idle_enable();
		amisc_plla_set_rate(data->pll_a_clk_rate);
		data->first_sync = 1;
		break;

	case EQOS_APE_AMISC_GET_RATE:
		/* configuring n_fract/n_modulo based on the rate table*/
		rate_info.rate = amisc_ape_get_rate();
		if (copy_to_user((struct rate_to_time_period *)_rate_info,
					&rate_info,
					sizeof(rate_info)))
			return -EFAULT;
		break;
	}

	return 0;
}

static int eqos_ape_ioctl_open(struct inode *inp, struct file *filep)
{
	int ret = 0;

	ret = pm_runtime_get_sync(&eqos_ape_drv_data->pdev->dev);
	if (ret < 0)
		return ret;

	dev_dbg(&eqos_ape_drv_data->pdev->dev, "eqos ape opened\n");
	amisc_idle_enable();
	return ret;
}

static int eqos_ape_ioctl_release(struct inode *inp, struct file *filep)
{
	int ret = 0;

	amisc_idle_disable();
	ret = pm_runtime_put_sync(&eqos_ape_drv_data->pdev->dev);
	if (ret < 0) {
		dev_err(&eqos_ape_drv_data->pdev->dev, "pm_runtime_put_sync failed\n");
		return ret;
	}
	dev_dbg(&eqos_ape_drv_data->pdev->dev, "eqos ape closed\n");
	return ret;
}

static const struct file_operations eqos_ape_ioctl_fops = {
	.owner = THIS_MODULE,
	.open = eqos_ape_ioctl_open,
	.release = eqos_ape_ioctl_release,
	.unlocked_ioctl = eqos_ape_hw_ioctl,
};

static void eqos_ape_ioctl_cleanup(void)
{

	amisc_clk_deinit();
	cdev_del(&eqos_ape_ioctl_cdev);
	device_destroy(eqos_ape_ioctl_class, MKDEV(eqos_ape_ioctl_major, 0));

	if (eqos_ape_ioctl_class)
		class_destroy(eqos_ape_ioctl_class);

	unregister_chrdev_region(
		MKDEV(eqos_ape_ioctl_major, 0), 1);
}

static int  eqos_ape_init(void)
{
	struct device *dev = &eqos_ape_drv_data->pdev->dev;
	dev_t eqos_ape_ioctl_dev;
	int ret = -ENODEV;
	int result;


	result = alloc_chrdev_region(&eqos_ape_ioctl_dev, 0,
		1, "eqos_ape_hw");
	if (result < 0)
		goto fail_err;

	eqos_ape_ioctl_major = MAJOR(eqos_ape_ioctl_dev);
	cdev_init(&eqos_ape_ioctl_cdev, &eqos_ape_ioctl_fops);
	eqos_ape_ioctl_cdev.owner = THIS_MODULE;
	eqos_ape_ioctl_cdev.ops = &eqos_ape_ioctl_fops;

	result = cdev_add(&eqos_ape_ioctl_cdev, eqos_ape_ioctl_dev, 1);
	if (result < 0)
		goto fail_chrdev;

	eqos_ape_ioctl_class = class_create(THIS_MODULE, "eqos_ape_hw");
	if (IS_ERR(eqos_ape_ioctl_class)) {
		pr_err(KERN_ERR "eqos_ape_hwdep: device class file already in use.\n");
		eqos_ape_ioctl_cleanup();
		return PTR_ERR(eqos_ape_ioctl_class);
	}

	dev_eqos_ape = device_create(eqos_ape_ioctl_class, NULL,
		MKDEV(eqos_ape_ioctl_major, 0),
		NULL, "%s", "eqos_ape_hw");

	dev_dbg(dev, "eqos ape init\n");
	return 0;

fail_chrdev:
	unregister_chrdev_region(eqos_ape_ioctl_dev, 1);

fail_err:
	return ret;
}

static void  eqos_ape_exit(void)
{
	eqos_ape_ioctl_cleanup();
}

static int eqos_ape_probe(struct platform_device *pdev)
{
	struct eqos_drvdata *drv_data;
	struct device *dev = &pdev->dev;
	struct resource *res = NULL;
	void __iomem *base = NULL;
	int ret = 0;
	int iter;


	drv_data = devm_kzalloc(dev, sizeof(*drv_data),
				GFP_KERNEL);
	drv_data->first_sync = 1;
	dev_set_drvdata(dev, drv_data);

	platform_set_drvdata(pdev, drv_data);
	drv_data->pdev = pdev;

	drv_data->base_regs = devm_kzalloc(dev, sizeof(void *),
				GFP_KERNEL);
	if (!drv_data->base_regs) {
		ret = -ENOMEM;
		goto out;
	}

	for (iter = 0; iter < AMISC_MAX_REG; iter++) {
		res = platform_get_resource(pdev, IORESOURCE_MEM, iter);
		if (!res) {
			dev_err(dev,
				"Failed to get resource with ID %d\n",
				iter);
			ret = -EINVAL;
			goto out;
		}
		base = devm_ioremap_nocache(dev,
				res->start,
				resource_size(res));
		if (IS_ERR_OR_NULL(base)) {
			dev_err(dev, "Failed to iomap resource reg[%d]\n",
				iter);
			ret = PTR_ERR(base);
			goto out;
		}
		drv_data->base_regs[iter] = base;
	}
	eqos_ape_drv_data = drv_data;
	eqos_ape_init();

	pm_runtime_enable(&eqos_ape_drv_data->pdev->dev);
	amisc_clk_init();
out:
	return ret;
}

static int eqos_ape_remove(struct platform_device *pdev)
{
	eqos_ape_exit();

	pm_runtime_disable(&eqos_ape_drv_data->pdev->dev);
	return 0;
}

#ifdef CONFIG_PM_SLEEP
static int eqos_ape_suspend(struct device *dev)
{
	struct eqos_drvdata *data = eqos_ape_drv_data;

	if (pm_runtime_status_suspended(dev))
		return 0;

	amisc_writel((AMISC_APE_TSC_CTRL_NMODULE_0_0_MASK(0) |
		AMISC_APE_TSC_CTRL_NFRACT_0_0_MASK(0)),
		AMISC_APE_TSC_CTRL_0_0);
	amisc_writel(AMISC_APE_TSC_CTRL_NINT_1_0_MASK(DEFAULT_N_INT),
		AMISC_APE_TSC_CTRL_1_0);
	amisc_writel(AMISC_APE_TSC_CTRL_3_0_DISABLE,
		AMISC_APE_TSC_CTRL_3_0);
	amisc_idle_enable();
	amisc_plla_set_rate(data->pll_a_clk_rate);
	data->first_sync = 1;

	pm_runtime_put_sync(dev);
	return 0;
}

static int eqos_ape_resume(struct device *dev)
{
	if (pm_runtime_status_suspended(dev))
		return 0;

	pm_runtime_get_sync(dev);

	amisc_idle_disable();
	amisc_writel(AMISC_APE_TSC_CTRL_3_0_ENABLE,
			AMISC_APE_TSC_CTRL_3_0);

	amisc_writel(
		(AMISC_APE_TSC_CTRL_NMODULE_0_0_MASK
		(g_rate.n_modulo) |
		AMISC_APE_TSC_CTRL_NFRACT_0_0_MASK
		(g_rate.n_fract)),
		AMISC_APE_TSC_CTRL_0_0);
	amisc_writel(AMISC_APE_TSC_CTRL_NINT_1_0_MASK
		(g_rate.n_int),
		AMISC_APE_TSC_CTRL_1_0);
	amisc_writel((AMISC_APE_TSC_CTRL_3_0_ENABLE |
		AMISC_APE_TSC_CTRL_3_0_COPY),
		AMISC_APE_TSC_CTRL_3_0);

	return 0;
}
#endif
static const struct dev_pm_ops eqos_ape_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(eqos_ape_suspend, eqos_ape_resume)
};

static const struct of_device_id eqos_ape_of_match[] = {
	{ .compatible = "nvidia,tegra18x-eqos-ape", .data = NULL, },
	{},
};

static struct platform_driver eqos_ape_driver = {
	.driver = {
		.name = "eqos_ape",
		.owner = THIS_MODULE,
		.pm = &eqos_ape_pm_ops,
		.of_match_table = of_match_ptr(eqos_ape_of_match),
	},
	.probe = eqos_ape_probe,
	.remove = eqos_ape_remove,
};

static int __init eqos_ape_modinit(void)
{
	return platform_driver_register(&eqos_ape_driver);
}
module_init(eqos_ape_modinit);

static void __exit eqos_ape_modexit(void)
{
	platform_driver_unregister(&eqos_ape_driver);
}
module_exit(eqos_ape_modexit);

MODULE_AUTHOR("Sidharth R V <svarier@nvidia.com>");
MODULE_DESCRIPTION("EQOS APE driver IO control of AMISC");
MODULE_LICENSE("GPL");
