/*
 * FPDLink Serializer driver
 *
 * Copyright (C) 2014-2021 NVIDIA CORPORATION. All rights reserved.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#include <linux/resource.h>
#include <linux/platform_device.h>
#include <linux/nvhost.h>
#include <linux/module.h>
#include <linux/i2c.h>
#include <linux/slab.h>
#include <linux/gpio.h>
#include <linux/of_gpio.h>
#include <linux/delay.h>
#include <linux/regmap.h>
#include <linux/list.h>

#include "dc.h"

#include "dc_priv.h"
#include "hdmi2fpd_ds90uh949.h"

static struct tegra_dc_hdmi2fpd_data *hdmi2fpd;

struct i2c_client_list {
	struct list_head i2c_list;
	int type;
	struct i2c_client *ds90uh949_i2c_client;
};

static LIST_HEAD(ds90uh949_i2c_list);
static DEFINE_MUTEX(i2c_list_mutex);

int hdmi2fpd_enable(struct tegra_dc *dc)
{
	struct tegra_dc_hdmi2fpd_data *hdmi2fpd = tegra_fpdlink_get_outdata(dc);

	if (hdmi2fpd && hdmi2fpd->hdmi2fpd_enabled)
		return 0;

	mutex_lock(&hdmi2fpd->lock);

	/* Turn on serializer chip */
	if (hdmi2fpd->en_gpio > 0)
		gpio_set_value(hdmi2fpd->en_gpio, 1);

	mdelay(hdmi2fpd->power_on_delay);

	hdmi2fpd->hdmi2fpd_enabled = true;
	mutex_unlock(&hdmi2fpd->lock);
	return 0;
}

void hdmi2fpd_disable(struct tegra_dc *dc)
{
	struct tegra_dc_hdmi2fpd_data *hdmi2fpd = tegra_fpdlink_get_outdata(dc);

	mutex_lock(&hdmi2fpd->lock);
	/* Turn off serializer chip */
	if (hdmi2fpd->en_gpio > 0)
		gpio_set_value(hdmi2fpd->en_gpio, 0);

	mdelay(hdmi2fpd->power_off_delay);

	hdmi2fpd->hdmi2fpd_enabled = false;
	mutex_unlock(&hdmi2fpd->lock);
}

#ifdef CONFIG_PM
void hdmi2fpd_suspend(struct tegra_dc *dc)
{
	hdmi2fpd_disable(dc);
}

int hdmi2fpd_resume(struct tegra_dc *dc)
{
	return hdmi2fpd_enable(dc);
}
#endif

static struct regmap_config hdmi2fpd_regmap_config = {
	.reg_bits = 8,
	.val_bits = 8,
};

static int of_hdmi2fpd_parse_platform_data(struct tegra_dc *dc,
						struct i2c_client *client)
{
	struct device_node *np = client->dev.of_node;
	enum of_gpio_flags flags;
	u32 temp;
	int err = 0;

	if (!np) {
		dev_err(&dc->ndev->dev,
				"hdmi2fpd: device node not defined in DT\n");
		err = -EINVAL;
		goto err;
	}

	hdmi2fpd->en_gpio = of_get_named_gpio_flags(np,
			"ti,enable-gpio", 0, &flags);
	hdmi2fpd->en_gpio_flags = flags;

	if (!hdmi2fpd->en_gpio) {
		dev_err(&dc->ndev->dev,
				"hdmi2fpd: gpio number not provided\n");
		err = -EINVAL;
		goto err;
	}

	if (!of_property_read_u32(np, "out-type", &temp))
		hdmi2fpd->out_type = temp;

	if (!of_property_read_u32(np, "ti,power-on-delay", &temp))
		hdmi2fpd->power_on_delay = temp;

	if (!of_property_read_u32(np, "ti,power-off-delay", &temp))
		hdmi2fpd->power_off_delay = temp;
err:
	return err;
}

struct i2c_client *get_i2c_client(struct tegra_dc *dc)
{
	struct list_head *pos;
	struct i2c_client_list *tmp;
	struct device_node *np;
	u32 sor_num;

	mutex_lock(&i2c_list_mutex);
	list_for_each(pos, &ds90uh949_i2c_list) {
		tmp = list_entry(pos, struct i2c_client_list, i2c_list);
		np = tmp->ds90uh949_i2c_client->dev.of_node;

		if (of_property_read_u32(np, "sor-instance", &sor_num))
			continue;

		if (dc && dc->out_ops &&
		    dc->out_ops->get_connector_instance &&
		    sor_num == dc->out_ops->get_connector_instance(dc)) {

			mutex_unlock(&i2c_list_mutex);
			return tmp->ds90uh949_i2c_client;
		}
	}
	mutex_unlock(&i2c_list_mutex);
	return NULL;
}

int hdmi2fpd_init(struct tegra_dc *dc)
{
	int err = 0;
	struct i2c_client *ds90uh949_i2c_client = NULL;

	hdmi2fpd = devm_kzalloc(&dc->ndev->dev,
				sizeof(*hdmi2fpd), GFP_KERNEL);
	if (!hdmi2fpd)
		return -ENOMEM;

	ds90uh949_i2c_client = get_i2c_client(dc);
	if (ds90uh949_i2c_client == NULL) {
		pr_err("i2c_client not found");
		return -ENODEV;
	}

	err = of_hdmi2fpd_parse_platform_data(dc, ds90uh949_i2c_client);
	if (err)
		return err;

	err = gpio_request(hdmi2fpd->en_gpio, "hdmi2fpd");
	if (err < 0) {
		pr_err("err %d: hdmi2fpd GPIO request failed\n", err);
		return err;
	}
	if (hdmi2fpd->en_gpio_flags & OF_GPIO_ACTIVE_LOW)
		gpio_direction_output(hdmi2fpd->en_gpio, 0);
	else
		gpio_direction_output(hdmi2fpd->en_gpio, 1);

	hdmi2fpd->regmap = devm_regmap_init_i2c(ds90uh949_i2c_client,
						&hdmi2fpd_regmap_config);
	if (IS_ERR(hdmi2fpd->regmap)) {
		err = PTR_ERR(hdmi2fpd->regmap);
		dev_err(&ds90uh949_i2c_client->dev,
				"Failed to allocate register map: %d\n", err);
		return err;
	}

	hdmi2fpd->dc = dc;

	tegra_fpdlink_set_outdata(dc, hdmi2fpd);

	mutex_init(&hdmi2fpd->lock);

	return err;
}

void hdmi2fpd_destroy(struct tegra_dc *dc)
{
	struct tegra_dc_hdmi2fpd_data *hdmi2fpd = tegra_fpdlink_get_outdata(dc);

	if (!hdmi2fpd)
		return;

	hdmi2fpd = NULL;
	mutex_destroy(&hdmi2fpd->lock);
}

static int ds90uh949_probe(struct i2c_client *client,
					const struct i2c_device_id *id)
{
	struct i2c_client_list *new;
	if (!i2c_check_functionality(client->adapter,
					I2C_FUNC_SMBUS_BYTE_DATA)) {
		dev_err(&client->dev, "SMBUS Byte Data not Supported\n");
		return -EIO;
	}
	new = kzalloc(sizeof(*new), GFP_KERNEL);
	if (new == NULL)
		return -ENOMEM;

	INIT_LIST_HEAD(&new->i2c_list);

	new->ds90uh949_i2c_client = client;
	mutex_lock(&i2c_list_mutex);
	list_add(&new->i2c_list, &ds90uh949_i2c_list);
	mutex_unlock(&i2c_list_mutex);
	return 0;
}

static int ds90uh949_remove(struct i2c_client *client)
{
	struct i2c_client_list *pos, *next;
	mutex_lock(&i2c_list_mutex);
	list_for_each_entry_safe(pos, next, &ds90uh949_i2c_list, i2c_list) {
		if (pos->ds90uh949_i2c_client == client) {
			list_del(&pos->i2c_list);
			break;
		}
	}
	mutex_unlock(&i2c_list_mutex);
	return 0;
}

static const struct i2c_device_id ds90uh949_id[] = {
	{ "ds90uh949", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, ds90uh949_id);

static struct of_device_id ds90uh949_of_match[] = {
	{.compatible = "ti,ds90uh949", },
	{ },
};

static struct i2c_driver ds90uh949_driver = {
	.driver = {
		.name = "ds90uh949",
		.owner	= THIS_MODULE,
		.of_match_table =
			of_match_ptr(ds90uh949_of_match),
	},
	.probe    = ds90uh949_probe,
	.remove   = ds90uh949_remove,
	.id_table = ds90uh949_id,
};

static int __init ds90uh949_i2c_client_init(void)
{
	int err = 0;

	err = i2c_add_driver(&ds90uh949_driver);
	if (err)
		pr_err("ds90uh949: Failed to add i2c client driver\n");

	return err;
}

static void __exit ds90uh949_i2c_client_exit(void)
{
	i2c_del_driver(&ds90uh949_driver);
}
subsys_initcall(ds90uh949_i2c_client_init);
module_exit(ds90uh949_i2c_client_exit);

MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("ds90uh949 FPDLink Serializer driver");
MODULE_ALIAS("i2c:ds90uh949_ser");

