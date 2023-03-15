/*
 * drivers/extcon/extcon-gpio-states.c
 *
 * Multiple GPIO state based based on extcon class driver.
 *
 * Copyright (c) 2014-2019, NVIDIA CORPORATION.  All rights reserved.
 *
 * Author: Laxman Dewangan <ldewangan@nvidia.com>
 *
 * Based on extcon-gpio driver by
 *	Copyright (C) 2008 Google, Inc.
 *	Author: Mike Lockwood <lockwood@android.com>
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/workqueue.h>
#include <linux/gpio.h>
#include <linux/extcon.h>
#include <linux/of.h>
#include <linux/of_gpio.h>
#include <linux/spinlock.h>
#include <linux/device.h>

#define EXTCON_GPIO_STATE_WAKEUP_TIME		5000

struct gpio_extcon_cables {
	int gstate;
	int cstate;
};

struct gpio_info {
	int gpio;
	int irq;
};

struct gpio_extcon_platform_data {
	const char *name;
	unsigned long debounce;
	unsigned long wait_for_gpio_scan;
	unsigned long irq_flags;
	struct gpio_info *gpios;
	int n_gpio;
	int *out_cable_name;
	int n_out_cables;
	struct gpio_extcon_cables *cable_states;
	int n_cable_states;
	int cable_detect_delay;
	int init_state;
	bool wakeup_source;
	int cable_id;
	bool has_extcon_none_state;
};

struct gpio_extcon_info {
	struct device *dev;
	struct extcon_dev *edev;
	struct delayed_work work;
	unsigned long debounce_jiffies;
	struct timer_list timer;
	int gpio_scan_work_jiffies;
	spinlock_t lock;
	int *gpio_curr_state;
	struct gpio_extcon_platform_data *pdata;
	struct wakeup_source wake_lock;
	bool wakeup_source;
	int last_cstate;
	unsigned int wakeup_cables;
	bool sysfs_controlled;
};

static void gpio_extcon_scan_work(struct work_struct *work)
{
	int state = 0;
	int cstate = -1;
	struct gpio_extcon_info	*gpex = container_of(to_delayed_work(work),
					struct gpio_extcon_info, work);
	int gstate = 0;
	int i;

	/* Skip update as it's already done in state_store through sysfs */
	if (gpex->last_cstate != gpex->edev->state) {
		if ((gpex->last_cstate == EXTCON_NONE) &&
				(gpex->edev->state == EXTCON_USB_HOST))
			gpex->sysfs_controlled = true;
		if ((gpex->last_cstate == EXTCON_USB_HOST) &&
				(gpex->edev->state == EXTCON_NONE))
			gpex->sysfs_controlled = false;

		gpex->last_cstate = gpex->edev->state;
		return;
	}

	/* Skip if userspace asked to switch to host mode */
	if (gpex->sysfs_controlled)
		return;

	for (i = 0; i < gpex->pdata->n_gpio; ++i) {
		state = gpio_get_value_cansleep(gpex->pdata->gpios[i].gpio);

		if (state)
			gstate |= BIT(i);
	}

	for (i = 0; i < gpex->pdata->n_cable_states; ++i) {
		if (gpex->pdata->cable_states[i].gstate == gstate) {
			cstate = gpex->pdata->cable_states[i].cstate;
			gpex->pdata->cable_id = cstate;
			break;
		}
	}

	if (cstate == -1) {
		dev_info(gpex->dev, "Cable state not found 0x%02x\n", gstate);
		cstate = 0;
	}

	/*
	 * Do default/general cable state overwrite
	 *
	 * The rule is:
	 * When last cable state is either EXTCON_USB_HOST or EXTCON_USB,
	 * any change of cable state should only be "disconnect" state
	 * (EXTCON_NONE).
	 *
	 * We override the state change only when the last state is host cable
	 * (EXTCON_USB_HOST). Because when ID becomes floating, VBUS is still
	 * supplied by host mode driver so VBUS detection GPIO will indicate
	 * we switch to device mode (EXTCON_USB), which is not possible
	 * physically and logically. We should move to disconnect state instead.
	 *
	 * Possible state transition:
	 *
	 * (host mode) <-> (disconnect/no cable) <-> (device mode)
	 *
	 * In cstate value:
	 * 0x2 <-> 0x0 <-> 0x1
	 */
	if (gpex->last_cstate != cstate) {
		if (gpex->pdata->has_extcon_none_state &&
		    gpex->last_cstate == EXTCON_USB_HOST) {
			cstate = EXTCON_NONE;
			gpex->pdata->cable_id = cstate;
		}

		if (gpex->last_cstate)
			extcon_set_state_sync(gpex->edev, gpex->last_cstate, 0);

		gpex->last_cstate = cstate;
	}

	dev_info(gpex->dev, "Cable state:%d, cable id:%d\n",
			!!cstate, gpex->pdata->cable_id);
	if (!gpex->pdata->cable_id)
		return;

	extcon_set_state_sync(gpex->edev, gpex->pdata->cable_id, !!cstate);
}

static void gpio_extcon_notifier_timer(unsigned long _data)
{
	struct gpio_extcon_info *gpex = (struct gpio_extcon_info *)_data;

	/*take wakelock to complete cable detection */
	if (!(gpex->wake_lock.active))
		__pm_wakeup_event(&gpex->wake_lock,
				  gpex->pdata->cable_detect_delay);

	schedule_delayed_work(&gpex->work, gpex->gpio_scan_work_jiffies);
}

static irqreturn_t gpio_irq_handler(int irq, void *dev_id)
{
	struct gpio_extcon_info *gpex = dev_id;
	unsigned long flags;

	spin_lock_irqsave(&gpex->lock, flags);
	mod_timer(&gpex->timer, jiffies + gpex->debounce_jiffies);
	spin_unlock_irqrestore(&gpex->lock, flags);

	return IRQ_HANDLED;
}

static struct gpio_extcon_platform_data *of_get_platform_data(
		struct platform_device *pdev)
{
	struct gpio_extcon_platform_data *pdata;
	struct device_node *np = pdev->dev.of_node;
	int gpio;
	int n_gpio;
	u32 pval;
	int ret;
	int count;

	pdata = devm_kzalloc(&pdev->dev, sizeof(*pdata), GFP_KERNEL);
	if (!pdata)
		return ERR_PTR(-ENOMEM);

	ret = of_property_read_string(np, "label", &pdata->name);
	if ((ret < 0) || !pdata->name)
		ret = of_property_read_string(np, "extcon-gpio,name", &pdata->name);
	if ((ret < 0) || !pdata->name)
		pdata->name = np->name;

	n_gpio = of_gpio_named_count(np, "gpios");
	if (n_gpio < 1) {
		ret = of_property_read_u32(np, "cable-connected-on-boot", &pval);
		pdata->init_state = (!ret) ? pval : -1;
		goto parse_cable_names;
	}

	pdata->n_gpio = n_gpio;
	pdata->gpios = devm_kzalloc(&pdev->dev,
			sizeof(*pdata->gpios) * n_gpio, GFP_KERNEL);
	if (!pdata->gpios)
		return ERR_PTR(-ENOMEM);
	for (count = 0; count < n_gpio; ++count) {
		gpio = of_get_named_gpio(np, "gpios", count);
		if ((gpio < 0) && (gpio != -ENOENT))
			return ERR_PTR(gpio);
		pdata->gpios[count].gpio = gpio;
	}

	ret = of_property_read_u32(np, "extcon-gpio,irq-flags", &pval);
	if (!ret)
		pdata->irq_flags = pval;
	else
		pdata->irq_flags = IRQF_TRIGGER_RISING |
						IRQF_TRIGGER_FALLING;

	ret = of_property_read_u32(np, "extcon-gpio,debounce", &pval);
	if (!ret)
		pdata->debounce = pval;
	else
		pdata->debounce = 10;

	ret = of_property_read_u32(np, "extcon-gpio,wait-for-gpio-scan", &pval);
	if (!ret)
		pdata->wait_for_gpio_scan = pval;
	else
		pdata->wait_for_gpio_scan = 100;

	ret = of_property_read_u32(np, "cable-detect-delay", &pval);
	if (!ret)
		pdata->cable_detect_delay = pval;
	else
		pdata->cable_detect_delay = EXTCON_GPIO_STATE_WAKEUP_TIME;

	pdata->wakeup_source = of_property_read_bool(np, "wakeup-source");

	pdata->n_cable_states = of_property_count_u32_elems(np,
						"extcon-gpio,cable-states");
	if (pdata->n_cable_states < 2) {
		dev_err(&pdev->dev, "not found proper cable state\n");
		return ERR_PTR(-EINVAL);
	}
	pdata->n_cable_states /= 2;
	pdata->cable_states = devm_kzalloc(&pdev->dev,
				(pdata->n_cable_states) *
				sizeof(*pdata->cable_states), GFP_KERNEL);
	if (!pdata->cable_states)
		return ERR_PTR(-ENOMEM);
	for (count = 0;  count < pdata->n_cable_states; ++count) {
		ret = of_property_read_u32_index(np, "extcon-gpio,cable-states",
				count * 2, &pval);
		if (!ret)
			pdata->cable_states[count].gstate = pval;

		ret = of_property_read_u32_index(np, "extcon-gpio,cable-states",
				count * 2 + 1, &pval);
		if (!ret) {
			pdata->cable_states[count].cstate = pval;
			if (pval == EXTCON_NONE)
				pdata->has_extcon_none_state = true;
		}
	}

parse_cable_names:
	pdata->n_out_cables = of_property_count_u32_elems(np,
					"extcon-gpio,out-cable-names");
	if (pdata->n_out_cables <= 0) {
		dev_err(&pdev->dev, "not found out cable names\n");
		return ERR_PTR(-EINVAL);
	}

	pdata->out_cable_name = devm_kzalloc(&pdev->dev,
				(pdata->n_out_cables + 1) *
				sizeof(*pdata->out_cable_name), GFP_KERNEL);
	if (!pdata->out_cable_name)
		return ERR_PTR(-ENOMEM);

	ret = of_property_read_u32_array(np,
			"extcon-gpio,out-cable-names",
			pdata->out_cable_name, pdata->n_out_cables);
	if (ret)
		return ERR_PTR(-EINVAL);

	return pdata;
}

static int gpio_extcon_probe(struct platform_device *pdev)
{
	struct gpio_extcon_platform_data *pdata = pdev->dev.platform_data;
	struct gpio_extcon_info *gpex;
	int ret = 0;
	int i;

	if (!pdata && pdev->dev.of_node) {
		pdata = of_get_platform_data(pdev);
		if (IS_ERR(pdata)) {
			dev_err(&pdev->dev, "extcon probe failed: %ld\n", PTR_ERR(pdata));
			return PTR_ERR(pdata);
		}
	}
	if (!pdata)
		return -EINVAL;

	if (!pdata->irq_flags && pdata->n_gpio) {
		dev_err(&pdev->dev, "IRQ flag is not specified.\n");
		return -EINVAL;
	}

	gpex = devm_kzalloc(&pdev->dev, sizeof(struct gpio_extcon_info),
				   GFP_KERNEL);
	if (!gpex)
		return -ENOMEM;

	gpex->dev = &pdev->dev;
	gpex->edev = devm_extcon_dev_allocate(&pdev->dev,
						pdata->out_cable_name);
	if (IS_ERR(gpex->edev)) {
		dev_err(&pdev->dev, "failed to allocate extcon device\n");
		return -ENOMEM;
	}

	gpex->edev->name = pdata->name;
	gpex->debounce_jiffies = msecs_to_jiffies(pdata->debounce);
	gpex->gpio_scan_work_jiffies = msecs_to_jiffies(
						pdata->wait_for_gpio_scan);
	gpex->wakeup_source = pdata->wakeup_source;
	gpex->pdata = pdata;
	spin_lock_init(&gpex->lock);

	for (i = 0; i < gpex->pdata->n_gpio; ++i) {
		int irq;

		irq =  gpio_to_irq(gpex->pdata->gpios[i].gpio);
		if (irq < 0) {
			dev_err(&pdev->dev, "gpio %d to irq failed: %d\n",
					gpex->pdata->gpios[i].gpio, irq);
			return irq;
		}
		gpex->pdata->gpios[i].irq = irq;
	}

	ret = devm_extcon_dev_register(&pdev->dev, gpex->edev);
	if (ret < 0)
		return ret;

	wakeup_source_init(&gpex->wake_lock, "extcon-suspend-lock");

	INIT_DELAYED_WORK(&gpex->work, gpio_extcon_scan_work);
	setup_timer(&gpex->timer, gpio_extcon_notifier_timer,
			(unsigned long)gpex);

	for (i = 0; i < gpex->pdata->n_gpio; ++i) {
		int gpio = gpex->pdata->gpios[i].gpio;
		int irq = gpex->pdata->gpios[i].irq;

		ret = devm_gpio_request_one(&pdev->dev, gpio, GPIOF_DIR_IN,
				    pdev->name);
		if (ret < 0)
			return ret;

		ret = devm_request_any_context_irq(&pdev->dev, irq,
				gpio_irq_handler, pdata->irq_flags,
				pdev->name, gpex);
		if (ret < 0)
			return ret;
	}

	platform_set_drvdata(pdev, gpex);

	if (gpex->wakeup_source)
		device_init_wakeup(gpex->dev, 1);

	/* Perform initial detection */
	if (gpex->pdata->n_gpio) {
		gpio_extcon_scan_work(&gpex->work.work);
	} else {
		if (pdata->init_state < 0) {
			dev_info(gpex->dev, "No Cable connected on boot\n");
			extcon_set_state_sync(gpex->edev,
				pdata->out_cable_name[0], false);
		} else {
			dev_info(gpex->dev, "Cable %d connected on boot\n",
				pdata->out_cable_name[pdata->init_state]);
			extcon_set_state_sync(gpex->edev,
				pdata->out_cable_name[pdata->init_state],
				BIT(pdata->init_state));
		}
	}

	return 0;
}

static int gpio_extcon_remove(struct platform_device *pdev)
{
	struct gpio_extcon_info *gpex = platform_get_drvdata(pdev);

	del_timer_sync(&gpex->timer);
	cancel_delayed_work_sync(&gpex->work);
	return 0;
}

#ifdef CONFIG_PM_SLEEP
static int gpio_extcon_suspend(struct device *dev)
{
	struct gpio_extcon_info *gpex = dev_get_drvdata(dev);
	int i, ret;

	cancel_delayed_work_sync(&gpex->work);
	if (device_may_wakeup(gpex->dev)) {
		for (i = 0; i < gpex->pdata->n_gpio; ++i) {
			ret = enable_irq_wake(gpex->pdata->gpios[i].irq);
			if (!ret)
				gpex->wakeup_cables |= BIT(i);
		}
	}

	return 0;
}

static int gpio_extcon_resume(struct device *dev)
{
	struct gpio_extcon_info *gpex = dev_get_drvdata(dev);
	int i;

	if (device_may_wakeup(gpex->dev)) {
		for (i = 0; i < gpex->pdata->n_gpio; ++i) {
			if ((gpex->wakeup_cables & BIT(i)) == 0)
				continue;
			gpex->wakeup_cables &= ~BIT(i);

			disable_irq_wake(gpex->pdata->gpios[i].irq);
		}
	}
	gpio_extcon_scan_work(&gpex->work.work);

	return 0;
}
#endif /* CONFIG_PM_SLEEP */

static SIMPLE_DEV_PM_OPS(gpio_extcon_pm_ops, gpio_extcon_suspend,
						gpio_extcon_resume);

static struct of_device_id of_extcon_gpio_tbl[] = {
	{ .compatible = "extcon-gpio-states", },
	{ /* end */ }
};
MODULE_DEVICE_TABLE(of, of_extcon_gpio_tbl);

static struct platform_driver gpio_extcon_driver = {
	.probe		= gpio_extcon_probe,
	.remove		= gpio_extcon_remove,
	.driver		= {
		.name	= "extcon-gpio-states",
		.owner	= THIS_MODULE,
		.of_match_table = of_extcon_gpio_tbl,
		.pm = &gpio_extcon_pm_ops,
	},
};

static int __init gpio_extcon_driver_init(void)
{
	return platform_driver_register(&gpio_extcon_driver);
}
subsys_initcall_sync(gpio_extcon_driver_init);

static void __exit gpio_extcon_driver_exit(void)
{
	platform_driver_unregister(&gpio_extcon_driver);
}
module_exit(gpio_extcon_driver_exit);

MODULE_AUTHOR("Laxman Dewangan <ldewangan@nvidia.com>");
MODULE_DESCRIPTION("GPIO state based extcon driver");
MODULE_LICENSE("GPL v2");
