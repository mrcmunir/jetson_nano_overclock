/*
 * Copyright (C) 2010 Google, Inc.
 *
 * Author:
 *	Colin Cross <ccross@google.com>
 *
 * Copyright (c) 2014-2018, NVIDIA CORPORATION.  All rights reserved.
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

#include <linux/kernel.h>
#include <linux/clk.h>
#include <linux/clkdev.h>
#include <linux/clk-provider.h>
#include <linux/debugfs.h>
#include <linux/init.h>
#include <linux/list.h>
#include <linux/list_sort.h>
#include <linux/module.h>
#include <linux/regulator/consumer.h>
#include <linux/suspend.h>
#include <linux/thermal.h>
#include <linux/reboot.h>
#include <linux/platform_device.h>
#include <linux/of.h>
#include <linux/pm_opp.h>
#include <linux/cpu.h>

#include <soc/tegra/tegra-dfll.h>
#include <soc/tegra/tegra-dvfs.h>

struct dvfs_rail *tegra_cpu_rail;
struct dvfs_rail *tegra_core_rail;
static struct dvfs_rail *tegra_gpu_rail;

bool core_dvfs_started;

static LIST_HEAD(dvfs_rail_list);
static DEFINE_MUTEX(dvfs_lock);

static int dvfs_rail_update(struct dvfs_rail *rail);

static inline int tegra_dvfs_rail_get_disable_level(struct dvfs_rail *rail)
{
	return rail->disable_millivolts ? : rail->nominal_millivolts;
}

static inline int tegra_dvfs_rail_get_suspend_level(struct dvfs_rail *rail)
{
	return rail->suspend_millivolts ? : rail->nominal_millivolts;
}

void tegra_dvfs_add_relationships(struct dvfs_relationship *rels, int n)
{
	int i;
	struct dvfs_relationship *rel;

	mutex_lock(&dvfs_lock);

	for (i = 0; i < n; i++) {
		rel = &rels[i];
		list_add_tail(&rel->from_node, &rel->to->relationships_from);
		list_add_tail(&rel->to_node, &rel->from->relationships_to);
	}

	mutex_unlock(&dvfs_lock);
}

static void init_rails_lists(struct dvfs_rail *rails[], int n)
{
	int i;
	static bool initialized;

	if (initialized)
		return;

	initialized = true;

	for (i = 0; i < n; i++) {
		INIT_LIST_HEAD(&rails[i]->dvfs);
		INIT_LIST_HEAD(&rails[i]->relationships_from);
		INIT_LIST_HEAD(&rails[i]->relationships_to);

		list_add_tail(&rails[i]->node, &dvfs_rail_list);
	}
}

void tegra_dvfs_init_rails_lists(struct dvfs_rail *rails[], int n)
{
	mutex_lock(&dvfs_lock);
	init_rails_lists(rails, n);
	mutex_unlock(&dvfs_lock);
}

int tegra_dvfs_init_rails(struct dvfs_rail *rails[], int n)
{
	int i, mv;

	mutex_lock(&dvfs_lock);

	init_rails_lists(rails, n);

	for (i = 0; i < n; i++) {
		mv = rails[i]->nominal_millivolts;
		if (rails[i]->disable_millivolts > mv)
			rails[i]->disable_millivolts = mv;
		if (rails[i]->suspend_millivolts > mv)
			rails[i]->suspend_millivolts = mv;

		rails[i]->millivolts = mv;
		rails[i]->new_millivolts = mv;
		if (!rails[i]->step)
			rails[i]->step = rails[i]->max_millivolts;
		if (!rails[i]->step_up)
			rails[i]->step_up = rails[i]->step;

		if (!strcmp("vdd-cpu", rails[i]->reg_id))
			tegra_cpu_rail = rails[i];
		else if (!strcmp("vdd-core", rails[i]->reg_id))
			tegra_core_rail = rails[i];
		else if (!strcmp("vdd-gpu", rails[i]->reg_id))
			tegra_gpu_rail = rails[i];
	}

	mutex_unlock(&dvfs_lock);

	return 0;
}

static int dvfs_solve_relationship(struct dvfs_relationship *rel)
{
	return rel->solve(rel->from, rel->to);
}

static void dvfs_rail_stats_init(struct dvfs_rail *rail, int millivolts)
{
	int dvfs_rail_stats_range;

	if (!rail->stats.bin_uv)
		rail->stats.bin_uv = DVFS_RAIL_STATS_BIN;

	dvfs_rail_stats_range =
		(DVFS_RAIL_STATS_TOP_BIN - 1) * rail->stats.bin_uv / 1000;

	rail->stats.last_update = ktime_get();
	if (millivolts >= rail->min_millivolts) {
		int i = 1 + (2 * (millivolts - rail->min_millivolts) * 1000 +
			     rail->stats.bin_uv) / (2 * rail->stats.bin_uv);
		rail->stats.last_index = min(i, DVFS_RAIL_STATS_TOP_BIN);
	}

	if (rail->max_millivolts >
	    rail->min_millivolts + dvfs_rail_stats_range)
		pr_warn("tegra_dvfs: %s: stats above %d mV will be squashed\n",
			rail->reg_id,
			rail->min_millivolts + dvfs_rail_stats_range);
}

static void dvfs_rail_stats_update(
	struct dvfs_rail *rail, int millivolts, ktime_t now)
{
	rail->stats.time_at_mv[rail->stats.last_index] = ktime_add(
		rail->stats.time_at_mv[rail->stats.last_index], ktime_sub(
			now, rail->stats.last_update));
	rail->stats.last_update = now;

	if (rail->stats.off)
		return;

	if (millivolts >= rail->min_millivolts) {
		int i = 1 + (2 * (millivolts - rail->min_millivolts) * 1000 +
			     rail->stats.bin_uv) / (2 * rail->stats.bin_uv);
		rail->stats.last_index = min(i, DVFS_RAIL_STATS_TOP_BIN);
	} else if (millivolts == 0)
			rail->stats.last_index = 0;
}

static int dvfs_rail_set_voltage_reg(struct dvfs_rail *rail, int millivolts)
{
	int ret;

	if (rail->joint_rail_with_dfll)
		tegra_dfll_set_external_floor_mv(rail->new_millivolts);

	ret = regulator_set_voltage(rail->reg,
		millivolts * 1000,
		rail->max_millivolts * 1000);

	return ret;
}

/**
 * dvfs_rail_set_voltage - set voltage in millivolts to specific rail
 *
 * @rail: struct dvfs_rail * power rail context
 * @millivolts: voltage in millivolts to be set to regulator
 *
 * Sets the voltage on a dvfs rail to a specific value, and updates any
 * rails that depend on this rail.
 */
static int dvfs_rail_set_voltage(struct dvfs_rail *rail, int millivolts)
{
	int ret = 0;
	struct dvfs_relationship *rel;
	int step, offset;
	int i;
	int steps;
	bool jmp_to_zero;

	if (!rail->reg) {
		if (millivolts == rail->millivolts)
			return 0;
		else
			return -EINVAL;
	}

	if (millivolts > rail->millivolts) {
		step = rail->step_up;
		offset = step;
	} else {
		step = rail->step;
		offset = -step;
	}

	if (rail->dfll_mode) {
		rail->millivolts = millivolts;
		rail->new_millivolts = millivolts;
		dvfs_rail_stats_update(rail, millivolts, ktime_get());
		return 0;
	}

	if (rail->disabled)
		return 0;

	rail->resolving_to = true;
	jmp_to_zero = rail->jmp_to_zero &&
			((millivolts == 0) || (rail->millivolts == 0));
	if (jmp_to_zero || (rail->in_band_pm && rail->stats.off))
		steps = 1;
	else
		steps = DIV_ROUND_UP(abs(millivolts - rail->millivolts), step);

	for (i = 0; i < steps; i++) {
		if ((i + 1) < steps)
			rail->new_millivolts = rail->millivolts + offset;
		else
			rail->new_millivolts = millivolts;

		/*
		 * Before changing the voltage, tell each rail that depends
		 * on this rail that the voltage will change.
		 * This rail will be the "from" rail in the relationship,
		 * the rail that depends on this rail will be the "to" rail.
		 * from->millivolts will be the old voltage
		 * from->new_millivolts will be the new voltage
		 */
		list_for_each_entry(rel, &rail->relationships_to, to_node) {
			ret = dvfs_rail_update(rel->to);
			if (ret)
				goto out;
		}

		ret = dvfs_rail_set_voltage_reg(rail, rail->new_millivolts);
		if (ret) {
			pr_err("Failed to set dvfs regulator %s\n",
					rail->reg_id);
			goto out;
		}

		rail->millivolts = rail->new_millivolts;
		dvfs_rail_stats_update(rail, rail->millivolts, ktime_get());

		/*
		 * After changing the voltage, tell each rail that depends
		 * on this rail that the voltage has changed.
		 * from->millivolts and from->new_millivolts will be the
		 * new voltage
		 */
		list_for_each_entry(rel, &rail->relationships_to, to_node) {
			ret = dvfs_rail_update(rel->to);
			if (ret)
				goto out;
		}
	}

	if (unlikely(rail->millivolts != millivolts)) {
		pr_err("%s: rail didn't reach target %d in %d steps (%d)\n",
			__func__, millivolts, steps, rail->millivolts);
		ret = -EINVAL;
	}

out:
	rail->resolving_to = false;
	return ret;
}

static int dvfs_rail_apply_limits(struct dvfs_rail *rail, int millivolts,
				  bool warn_on_cap)
{
	int min_mv = rail->min_millivolts;
	int max_mv = rail->max_millivolts;

	if (rail->therm_floors) {
		int i = rail->therm_floor_idx;

		if (i < rail->therm_floors_size)
			min_mv = rail->therm_floors[i].mv;
	}

	if (rail->therm_caps && warn_on_cap) {
		int i = rail->therm_cap_idx;

		if ((i > 0) && (millivolts > rail->therm_caps[i - 1].mv)) {
			WARN(!rail->therm_cap_warned,
			     "tegra_dvfs: %s set to %dmV above cap %dmV\n",
			     rail->reg_id, millivolts,
			     rail->therm_caps[i - 1].mv);
			rail->therm_cap_warned = true;
		} else {
			rail->therm_cap_warned = false;
		}
	}

	if (rail->override_millivolts) {
		millivolts = rail->override_millivolts;
		return millivolts;
	} else if (rail->dbg_mv_offs) {
		/* apply offset and ignore limits */
		millivolts += rail->dbg_mv_offs;
		return millivolts;
	}

	millivolts = clamp_val(millivolts, min_mv, max_mv);

	return millivolts;
}

/**
 * dvfs_rail_update - update rail voltage
 *
 * @rail: struct dvfs_rail * power rail context
 *
 * Determine the minimum valid voltage for a rail, taking into account
 * the dvfs clocks and any rails that this rail depends on.  Calls
 * dvfs_rail_set_voltage with the new voltage, which will call
 * dvfs_rail_update on any rails that depend on this rail.
 */
static int dvfs_rail_update(struct dvfs_rail *rail)
{
	int millivolts = 0;
	struct dvfs *d;
	struct dvfs_relationship *rel;
	int ret = 0;
	int steps;

	if (rail->disabled)
		return 0;

	/* if dvfs is suspended, return and handle it during resume */
	if (rail->suspended)
		return 0;

	/* if regulators are not connected yet, return and handle it later */
	if (!rail->reg)
		return 0;

	/* if rail update is entered while resolving circular dependencies,
	   abort recursion */
	if (rail->resolving_to)
		return 0;

	/* Find the maximum voltage requested by any clock */
	list_for_each_entry(d, &rail->dvfs, reg_node)
		millivolts = max(d->cur_millivolts, millivolts);

	/* Apply offset and min/max limits if any clock is requesting voltage */
	if (millivolts)
		millivolts = dvfs_rail_apply_limits(rail, millivolts, true);
	/* Keep current voltage if regulator is to be disabled via explicitly */
	else if (rail->in_band_pm)
		return 0;
	/* Keep current voltage if regulator must not be disabled at run time */
	else if (!rail->jmp_to_zero) {
		WARN(1, "%s cannot be turned off by dvfs\n", rail->reg_id);
		return 0;
	}

	/*
	 * retry update if limited by from-relationship to account for
	 * circular dependencies
	 */
	steps = DIV_ROUND_UP(abs(millivolts - rail->millivolts), rail->step);
	for (; steps >= 0; steps--) {
		rail->new_millivolts = millivolts;

		/* Check any rails that this rail depends on */
		list_for_each_entry(rel, &rail->relationships_from, from_node)
			rail->new_millivolts = dvfs_solve_relationship(rel);

		if (rail->new_millivolts == rail->millivolts)
			break;

		ret = dvfs_rail_set_voltage(rail, rail->new_millivolts);
	}

	return ret;
}

static int dvfs_rail_connect_to_regulator(struct device *dev,
					  struct dvfs_rail *rail)
{
	struct regulator *reg;
	int v;

	if (!rail->reg) {
		mutex_unlock(&dvfs_lock);
		reg = regulator_get(dev, rail->reg_id);
		mutex_lock(&dvfs_lock);
		if (IS_ERR(reg)) {
			pr_err("tegra_dvfs: failed to connect %s rail\n",
			       rail->reg_id);
			return PTR_ERR(reg);
		}
		rail->reg = reg;
	}

	if (!rail->in_band_pm) {
		v = regulator_enable(rail->reg);
		if (v < 0) {
			pr_err("tegra_dvfs: failed on enabling regulator %s\n, err %d",
				rail->reg_id, v);
			return v;
		}
	}

	v = regulator_get_voltage(rail->reg);
	if (v < 0) {
		pr_err("tegra_dvfs: failed initial get %s voltage\n",
		       rail->reg_id);
		return v;
	}

	if (!rail->min_millivolts) {
		int min_uv, max_uv;

		if (!regulator_get_constraint_voltages(rail->reg, &min_uv,
						       &max_uv))
			rail->min_millivolts = min_uv / 1000;
	}

	if (v > rail->nominal_millivolts * 1000) {
		if (dvfs_rail_set_voltage_reg(rail, rail->nominal_millivolts)) {
			pr_err("tegra_dvfs: failed lower %s voltage %d to %d\n",
			       rail->reg_id, v, rail->nominal_millivolts);
			return -EINVAL;
		}
		v = rail->nominal_millivolts * 1000;
	}

	rail->millivolts = v / 1000;
	rail->new_millivolts = rail->millivolts;
	dvfs_rail_stats_init(rail, rail->millivolts);

	return 0;
}

static inline const int *dvfs_get_millivolts_pll(struct dvfs *d)
{
	if (d->therm_dvfs) {
		int therm_idx = d->dvfs_rail->therm_scale_idx;

		return d->millivolts + therm_idx * MAX_DVFS_FREQS;
	}

	return d->millivolts;
}

static inline const int *dvfs_get_millivolts(struct dvfs *d, unsigned long rate)
{
	if (tegra_dvfs_is_dfll_scale(d, rate))
		return d->dfll_millivolts;

	return dvfs_get_millivolts_pll(d);
}

static inline const int *dvfs_get_peak_millivolts(struct dvfs *d, unsigned long rate)
{
	if (tegra_dvfs_is_dfll_scale(d, rate))
		return d->dfll_millivolts;

	if (d->peak_millivolts)
		return d->peak_millivolts;

	return dvfs_get_millivolts_pll(d);
}

static unsigned long *dvfs_get_freqs(struct dvfs *d)
{
	if (d->use_alt_freqs)
		return &d->alt_freqs[0];
	else
		return &d->freqs[0];
}

static int __tegra_dvfs_set_rate(struct dvfs *d, unsigned long rate)
{
	int i = 0;
	int ret, mv;
	unsigned long *freqs = dvfs_get_freqs(d);
	const int *millivolts = dvfs_get_millivolts(d, rate);

	if (freqs == NULL || millivolts == NULL)
		return -ENODEV;

	/*
	 * On entry to dfll range limit 1st step to range bottom (full ramp of
	 * voltage/rate is completed automatically in dfll mode)
	 */
	if (tegra_dvfs_is_dfll_range_entry(d, rate))
		rate = d->use_dfll_rate_min;

	if (rate > freqs[d->num_freqs - 1]) {
		pr_warn("tegra-dvfs: rate %lu too high for dvfs on %s\n", rate,
			d->clk_name);
		return -EINVAL;
	}

	if (rate == 0) {
		d->cur_millivolts = 0;
	} else {
		while (i < d->num_freqs && rate > freqs[i])
			i++;

		if ((d->max_millivolts) &&
		    (millivolts[i] > d->max_millivolts)) {
			pr_warn("tegra-dvfs: voltage %d too high for dvfs on %s\n",
					millivolts[i], d->clk_name);
			return -EINVAL;
		}

		mv = millivolts[i];
		d->cur_millivolts = millivolts[i];
	}

	d->cur_rate = rate;

	ret = dvfs_rail_update(d->dvfs_rail);
	if (ret)
		pr_err("Failed to set regulator %s for clock %s to %d mV\n",
			d->dvfs_rail->reg_id, d->clk_name, d->cur_millivolts);

	return ret;
}

static struct dvfs *tegra_clk_to_dvfs(struct clk *c)
{
	struct dvfs *d;
	struct dvfs_rail *rail;

	list_for_each_entry(rail, &dvfs_rail_list, node) {
		list_for_each_entry(d, &rail->dvfs, reg_node) {
			if (clk_is_match(c, d->clk))
				return d;
		}
	}
	return NULL;
}

/*
 *  Using non alt frequencies always results in peak voltage
 * (enforced by alt_freqs_validate())
 */
static int predict_non_alt_millivolts(struct dvfs *d, const int *millivolts,
				      unsigned long rate)
{
	int i;
	int vmin = d->dvfs_rail->min_millivolts;
	unsigned long dvfs_unit = 1 * d->freqs_mult;

	if (!millivolts)
		return -ENODEV;

	if (millivolts == d->dfll_millivolts)
		vmin = tegra_dfll_get_min_millivolts();

	for (i = 0; i < d->num_freqs; i++) {
		unsigned long f = d->freqs[i];
		if ((dvfs_unit < f) && (rate <= f))
			break;
	}

	if (i == d->num_freqs)
		i--;

	return max(millivolts[i], vmin);
}

static int predict_millivolts(struct dvfs *d, const int *millivolts,
			      unsigned long rate)
{
	int i;
	unsigned long *freqs = dvfs_get_freqs(d);

	if (!millivolts)
		return -ENODEV;

	for (i = 0; i < d->num_freqs; i++) {
		if (rate <= freqs[i])
			break;
	}

	if (i == d->num_freqs)
		return -EINVAL;

	return millivolts[i];
}

static int dvfs_rail_get_thermal_floor(struct dvfs_rail *rail)
{
	if (rail->therm_floors &&
		rail->therm_floor_idx < rail->therm_floors_size)
		return rail->therm_floors[rail->therm_floor_idx].mv;

	return 0;
}

static int dvfs_dfll_get_peak_thermal_floor(void)
{
	struct rail_alignment *align = tegra_dfll_get_alignment();
	int mv;

	if (WARN_ON(IS_ERR(align)))
		return 0;

	mv = tegra_dfll_get_peak_thermal_floor_mv();
	if (mv < 0)
		return 0;

	return tegra_round_voltage(mv, align, 1);
}

static int dvfs_get_peak_thermal_floor(struct dvfs *d, unsigned long rate)
{
	bool dfll_range = dvfs_is_dfll_range(d, rate);

	if (!dfll_range && d->dvfs_rail->therm_floors)
		return d->dvfs_rail->therm_floors[0].mv;
	if (dfll_range)
		return dvfs_dfll_get_peak_thermal_floor();
	return 0;
}

static int predict_mv_at_hz_no_tfloor(struct dvfs *d, unsigned long rate)
{
	const int *millivolts;

	millivolts = dvfs_get_millivolts(d, rate);

	return predict_millivolts(d, millivolts, rate);
}

static int predict_mv_at_hz_cur_tfloor(struct dvfs *d, unsigned long rate)
{
	int mv;

	mv = predict_mv_at_hz_no_tfloor(d, rate);

	if (mv < 0)
		return mv;

	return max(mv, dvfs_rail_get_thermal_floor(d->dvfs_rail));
}

int opp_millivolts[MAX_DVFS_FREQS];
unsigned long opp_frequencies[MAX_DVFS_FREQS];

/**
 * tegra_get_cpu_fv_table - get CPU frequencies/voltages table
 *
 * @num_freqs: number of frequencies
 * @freqs: the array of frequencies
 * @mvs: the array of voltages
 *
 * Get the frequency and voltage table using CPU OPP which were built by the
 * DFLL driver.
 */
int tegra_get_cpu_fv_table(int *num_freqs, unsigned long **freqs, int **mvs)
{
	struct device *cpu_dev;
	struct dev_pm_opp *opp;
	unsigned long rate;
	int i, ret = 0;

	cpu_dev = get_cpu_device(0);
	if (!cpu_dev)
		return -EINVAL;

	mutex_lock(&dvfs_lock);
	for (i = 0, rate = 0;; rate++) {
		rcu_read_lock();
		opp = dev_pm_opp_find_freq_ceil(cpu_dev, &rate);
		if (IS_ERR(opp)) {
			rcu_read_unlock();
			break;
		}
		opp_frequencies[i] = rate;
		opp_millivolts[i++] = dev_pm_opp_get_voltage(opp);
		rcu_read_unlock();
	}
	if (!i) {
		ret = -EINVAL;
		goto out;
	}

	*num_freqs = i;
	*freqs = opp_frequencies;
	*mvs = opp_millivolts;
out:
	mutex_unlock(&dvfs_lock);
	return ret;
}
EXPORT_SYMBOL(tegra_get_cpu_fv_table);

/**
 * tegra_dvfs_predict_millivolts - return the safe voltage for running
 *					the clock at one sepcific rate
 *
 * @c: struct clk * the clock which needs the voltage info
 * @rate: the rate being predicted
 *
 * Extract the voltage table associated with the clock and return the safe
 * voltage for ticking the clock at the specified rate
 */
int tegra_dvfs_predict_millivolts(struct clk *c, unsigned long rate)
{
	int mv;
	struct dvfs *d;

	d = tegra_clk_to_dvfs(c);
	if (d == NULL)
		return -EINVAL;

	if (!rate)
		return 0;

	mutex_lock(&dvfs_lock);

	mv = predict_mv_at_hz_no_tfloor(d, rate);

	mutex_unlock(&dvfs_lock);

	return mv;
}
EXPORT_SYMBOL(tegra_dvfs_predict_millivolts);

int tegra_dvfs_predict_mv_at_hz_cur_tfloor(struct clk *c, unsigned long rate)
{
	int mv;
	struct dvfs *d;

	d = tegra_clk_to_dvfs(c);
	if (d == NULL)
		return -EINVAL;

	if (!rate)
		return 0;

	mutex_lock(&dvfs_lock);

	mv = predict_mv_at_hz_cur_tfloor(d, rate);

	mutex_unlock(&dvfs_lock);

	return mv;
}
EXPORT_SYMBOL(tegra_dvfs_predict_mv_at_hz_cur_tfloor);

/*
 * Predict minimum voltage required to run target clock at specified rate.
 * Evaluate target clock domain V/F relation, and apply proper PLL or
 * DFLL table depending on specified rate range. Apply maximum thermal floor
 * across all temperature ranges.
 */
static int dvfs_predict_mv_at_hz_max_tfloor(struct dvfs *d, unsigned long rate)
{
	int mv;
	const int *millivolts;

	if (!d)
		return -ENODATA;

	millivolts = dvfs_get_peak_millivolts(d, rate);
	mv = predict_non_alt_millivolts(d, millivolts, rate);
	if (mv < 0)
		return mv;

	return max(mv, dvfs_get_peak_thermal_floor(d, rate));
}

int tegra_dvfs_predict_mv_at_hz_max_tfloor(struct clk *c, unsigned long rate)
{
	struct dvfs *d;
	int mv;

	d = tegra_clk_to_dvfs(c);
	if (d == NULL)
		return -EINVAL;

	mutex_lock(&dvfs_lock);
	mv = dvfs_predict_mv_at_hz_max_tfloor(d, rate);
	mutex_unlock(&dvfs_lock);

	return mv;
}
EXPORT_SYMBOL(tegra_dvfs_predict_mv_at_hz_max_tfloor);

long tegra_dvfs_predict_hz_at_mv_max_tfloor(struct clk *c, int mv)
{
	int mv_at_f = 0, i;
	struct dvfs *d;
	const int *millivolts;
	unsigned long rate = -EINVAL;

	/* Recursively search for ancestor with DVFS */
	do {
		d = tegra_clk_to_dvfs(c);
		if (!d)
			c = clk_get_parent(c);
	} while (!d && !IS_ERR_OR_NULL(c));

	if (d == NULL)
		return -EINVAL;

	mutex_lock(&dvfs_lock);

	if (d->alt_freqs)
		goto out;

	for (i = 0; i < d->num_freqs; i++) {
		rate = d->freqs[i];
		millivolts = dvfs_get_peak_millivolts(d, rate);
		if (!millivolts)
			goto out;

		if (d->dvfs_rail->therm_floors)
			mv_at_f = d->dvfs_rail->therm_floors[0].mv;
		mv_at_f = max(millivolts[i], mv_at_f);
		if (mv < mv_at_f)
			break;
	}

	if (i)
		rate = d->freqs[i - 1];
	if (!i || (rate <= d->freqs_mult))
		rate = -ENOENT;

out:
	mutex_unlock(&dvfs_lock);

	return rate;
}

/**
 * tegra_dvfs_set_rate - update rail voltage due to the clock rate change
 *
 * @c: struct clk * the clock which has changed rate
 * @rate: the changed rate
 *
 * Check if the voltage of the power rail need to be updated due to the clock
 * rate change.
 */
int tegra_dvfs_set_rate(struct clk *c, unsigned long rate)
{
	int ret = 0;
	struct dvfs *d;

	if (!core_dvfs_started)
		return ret;

	mutex_lock(&dvfs_lock);

	d = tegra_clk_to_dvfs(c);
	if (d)
		ret = __tegra_dvfs_set_rate(d, rate);

	mutex_unlock(&dvfs_lock);

	return ret;
}
EXPORT_SYMBOL(tegra_dvfs_set_rate);

/**
 * tegra_dvfs_get_rate - get current rate used for determining rail voltage
 *
 * @c: struct clk * clock we want to know the rate of used for determining
 *		    rail voltage
 *
 * Returns 0 if there is no dvfs for the clock.
 */
unsigned long tegra_dvfs_get_rate(struct clk *c)
{
	unsigned long rate = 0;
	struct dvfs *d;

	if (!core_dvfs_started)
		return rate;

	mutex_lock(&dvfs_lock);

	d = tegra_clk_to_dvfs(c);
	if (d)
		rate = d->cur_rate;

	mutex_unlock(&dvfs_lock);

	return rate;
}
EXPORT_SYMBOL(tegra_dvfs_get_rate);

/**
 * tegra_dvfs_get_freqs - export dvfs frequency array associated with the clock
 *
 * @c: struct clk * the clock which needs the frequency table
 * @freqs: the array of the frequencies
 * @num_freqs: number of the frequencies
 *
 * Check if the voltage of the power rail need to be updated due to the clock
 * rate change.
 */
int tegra_dvfs_get_freqs(struct clk *c, unsigned long **freqs, int *num_freqs)
{
	struct dvfs *d;

	d = tegra_clk_to_dvfs(c);
	if (d == NULL) {
		if (core_dvfs_started) {
			pr_err("Failed to get %s dvfs structure\n", __clk_get_name(c));
			return -ENOSYS;
		}
		return -EINVAL;
	}

	*num_freqs = d->num_freqs;
	*freqs = dvfs_get_freqs(d);

	return 0;
}
EXPORT_SYMBOL(tegra_dvfs_get_freqs);

unsigned long tegra_dvfs_get_maxrate(struct clk *c)
{
	unsigned long rate = 0;
	int err, num_freqs;
	unsigned long *freqs;

	err = tegra_dvfs_get_freqs(c, &freqs, &num_freqs);
	if (err < 0)
		return rate;

	return freqs[num_freqs - 1];
}
EXPORT_SYMBOL(tegra_dvfs_get_maxrate);

unsigned long tegra_dvfs_round_rate(struct clk *c, unsigned long rate)
{
	int i, num_freqs;
	unsigned long *freqs, ret;

	mutex_lock(&dvfs_lock);
	ret = tegra_dvfs_get_freqs(c, &freqs, &num_freqs);
	if (IS_ERR_VALUE(ret))
		goto out;

	for (i = 0; i < num_freqs; i++) {
		if (freqs[i] >= rate) {
			ret = freqs[i];
			goto out;
		}
	}
	ret = freqs[i-1];

out:
	mutex_unlock(&dvfs_lock);
	return ret;
}

int tegra_dvfs_use_alt_freqs_on_clk(struct clk *c, bool use_alt_freq)
{
	struct dvfs *d;
	int err = -ENOENT;

	mutex_lock(&dvfs_lock);

	d = tegra_clk_to_dvfs(c);
	if (d && d->alt_freqs) {
		err = 0;
		if (d->use_alt_freqs != use_alt_freq) {
			d->use_alt_freqs = use_alt_freq;
			if (__tegra_dvfs_set_rate(d, d->cur_rate) < 0) {
				d->use_alt_freqs = !use_alt_freq;
				pr_err("%s: %s: %s alt dvfs failed\n", __func__,
					d->clk_name,
					use_alt_freq ? "set" : "clear");
				__tegra_dvfs_set_rate(d, d->cur_rate);
				err = -EINVAL;
			}
		}
	}

	mutex_unlock(&dvfs_lock);

	return err;
}
EXPORT_SYMBOL(tegra_dvfs_use_alt_freqs_on_clk);

static int tegra_dvfs_clk_event(struct notifier_block *this,
		unsigned long event, void *ptr)
{
	struct clk_notifier_data *cnd = ptr;
	struct dvfs *d;
	int new_mv, err = 0;

	d = tegra_clk_to_dvfs(cnd->clk);
	if (d == NULL)
		return NOTIFY_DONE;

	if (d->dvfs_rail == tegra_core_rail && !core_dvfs_started)
		return NOTIFY_DONE;

	if (!__clk_is_enabled(cnd->clk) && !__clk_is_prepared(cnd->clk) &&
	    (d->dvfs_rail != tegra_gpu_rail))
		return NOTIFY_DONE;

	mutex_lock(&dvfs_lock);

	switch (event) {
	case PRE_RATE_CHANGE:
		if (d->therm_dvfs) {
			new_mv = predict_mv_at_hz_cur_tfloor(d, cnd->new_rate);
			if (new_mv > d->cur_millivolts)
				err = __tegra_dvfs_set_rate(d, cnd->new_rate);
		} else if (cnd->old_rate < cnd->new_rate) {
			err = __tegra_dvfs_set_rate(d, cnd->new_rate);
		}
		break;
	case POST_RATE_CHANGE:
		if (d->therm_dvfs) {
			new_mv = predict_mv_at_hz_cur_tfloor(d, cnd->new_rate);
			if (new_mv < d->cur_millivolts)
				err = __tegra_dvfs_set_rate(d, cnd->new_rate);
		} else if (cnd->old_rate > cnd->new_rate) {
			err = __tegra_dvfs_set_rate(d, cnd->new_rate);
		}
		break;
	case ABORT_RATE_CHANGE:
		break;
	}

	mutex_unlock(&dvfs_lock);

	if (err < 0)
		return NOTIFY_BAD;

	return NOTIFY_DONE;
}

static struct notifier_block tegra_dvfs_nb = {
	.notifier_call = tegra_dvfs_clk_event,
	.priority = 1,
};

static int use_alt_freq_get(void *data, u64 *val)
{
	struct clk *c = (struct clk *)data;
	struct dvfs *d;

	d = tegra_clk_to_dvfs(c);
	if (!d)
		*val = 0;
	else
		*val = d->use_alt_freqs;

	return 0;
}

static int use_alt_freq_set(void *data, u64 val)
{
	struct clk *c = (struct clk *)data;

	return tegra_dvfs_use_alt_freqs_on_clk(c, !!val);
}
DEFINE_SIMPLE_ATTRIBUTE(use_alt_freq_fops,
			use_alt_freq_get, use_alt_freq_set, "%llu\n");

static void cleanup_dvfs_table(struct dvfs *d)
{
	int i;

	for (i = 0; i < MAX_DVFS_FREQS; i++) {
		if (d->millivolts[i] == 0)
			break;

		if (d->freqs_mult)
			d->freqs[i] *= d->freqs_mult;

		/* If final frequencies are 0, pad with previous frequency */
		if (d->freqs[i] == 0 && i > 1)
			d->freqs[i] = d->freqs[i - 1];
	}

	/* Update num_freqs if not set at all, or set above cleaned max */
	if (!d->num_freqs || (d->num_freqs > i))
		d->num_freqs = i;
}

#ifdef CONFIG_TEGRA_CLK_DEBUG
static int dvfs_freq_offset_get(void *data, u64 *val)
{
	struct dvfs *d = data;

	*val = d->dbg_hz_offs;

	return 0;
}

static int dvfs_freq_offset_set(void *data, u64 val)
{
	struct dvfs *d = data;
	int i, ret = 0;
	long offs = (long)val - d->dbg_hz_offs;
	unsigned long unit_rate = 1 * d->freqs_mult;

	if (!offs || !d->num_freqs)
		return 0;

	mutex_lock(&dvfs_lock);

	for (i = 0; i <  d->num_freqs; i++) {
		unsigned long rate = d->freqs[i];

		if (rate <= unit_rate)
			continue;

		if ((offs < 0) && (rate <= unit_rate + (-offs))) {
			ret = -EINVAL;
			goto out;
		}

		d->freqs[i] = rate + offs;
	}
	d->dbg_hz_offs = (long)val;

out:
	mutex_unlock(&dvfs_lock);

	return ret;
}
DEFINE_SIMPLE_ATTRIBUTE(dvfs_freq_offset_fops, dvfs_freq_offset_get,
			dvfs_freq_offset_set, "%lld\n");
#endif

int tegra_setup_dvfs(struct clk *c, struct dvfs *d)
{
	cleanup_dvfs_table(d);

	d->clk = c;

	mutex_lock(&dvfs_lock);
	list_add_tail(&d->reg_node, &d->dvfs_rail->dvfs);
	mutex_unlock(&dvfs_lock);

#ifdef CONFIG_TEGRA_CLK_DEBUG
	__clk_debugfs_add_file(c, "dvfs_freq_offs", S_IRUGO | S_IWUSR, d,
				&dvfs_freq_offset_fops);
#endif
	return 0;
}

int tegra_dvfs_add_alt_freqs(struct clk *c, struct dvfs *alt_d)
{
	struct dvfs *d;
	int err = 0;

	mutex_lock(&dvfs_lock);

	d = tegra_clk_to_dvfs(c);
	if (!d) {
		err = -EINVAL;
		goto out;
	}

	cleanup_dvfs_table(alt_d);

	if (alt_d->num_freqs < d->num_freqs) {
		pr_err("tegra_dvfs: %s: %d alt freqs below %d main freqs\n",
		       d->clk_name, alt_d->num_freqs, d->num_freqs);
		err = -EINVAL;
		goto out;
	}

	d->alt_freqs = alt_d->freqs;

	__clk_debugfs_add_file(c, "use_alt_freq", S_IRUGO | S_IWUSR, c,
			       &use_alt_freq_fops);

out:
	mutex_unlock(&dvfs_lock);

	return err;
}

static bool tegra_dvfs_all_rails_suspended(void)
{
	struct dvfs_rail *rail;

	list_for_each_entry(rail, &dvfs_rail_list, node)
		if (!rail->suspended && !rail->disabled)
			return false;

	return true;
}

static bool tegra_dvfs_from_rails_suspended_or_solved(struct dvfs_rail *to)
{
	struct dvfs_relationship *rel;

	list_for_each_entry(rel, &to->relationships_from, from_node)
		if ((!rel->from->suspended) &&
		    (!rel->from->disabled) &&
		    (!rel->solved_at_nominal))
			return false;

	return true;
}

static int tegra_dvfs_suspend_one(void)
{
	struct dvfs_rail *rail;
	int mv;
	int ret = 0;

	list_for_each_entry(rail, &dvfs_rail_list, node) {
		if ((rail->suspended) ||
		    (rail->disabled) ||
		    (!tegra_dvfs_from_rails_suspended_or_solved(rail)))
			continue;

		mv = tegra_dvfs_rail_get_suspend_level(rail);
		mv = dvfs_rail_apply_limits(rail, mv, false);
		/* apply suspend limit only if it is above current mv */
		if (mv >= rail->millivolts)
			ret = dvfs_rail_set_voltage(rail, mv);
		if (ret) {
			pr_err("tegra_dvfs: failed %s suspend at %d\n",
			       rail->reg_id, rail->millivolts);
			return ret;
		}

		rail->suspended = true;
		return 0;
	}
	return -EINVAL;
}

static void tegra_dvfs_resume(void)
{
	struct dvfs_rail *rail;

	mutex_lock(&dvfs_lock);

	list_for_each_entry(rail, &dvfs_rail_list, node)
		rail->suspended = false;

	list_for_each_entry(rail, &dvfs_rail_list, node)
		dvfs_rail_update(rail);

	mutex_unlock(&dvfs_lock);
}

static int tegra_dvfs_suspend(void)
{
	int ret = 0;

	mutex_lock(&dvfs_lock);

	while (!tegra_dvfs_all_rails_suspended()) {
		ret = tegra_dvfs_suspend_one();
		if (ret)
			break;
	}

	mutex_unlock(&dvfs_lock);

	if (ret)
		tegra_dvfs_resume();

	return ret;
}

int tegra_dvfs_init_thermal_dvfs_voltages(int *therm_voltages,
	int *peak_voltages, int freqs_num, int ranges_num, struct dvfs *d)
{
	int *millivolts;
	int freq_idx, therm_idx;

	for (therm_idx = 0; therm_idx < ranges_num; therm_idx++) {
		millivolts = therm_voltages + therm_idx * MAX_DVFS_FREQS;
		for (freq_idx = 0; freq_idx < freqs_num; freq_idx++) {
			int mv = millivolts[freq_idx];
			if ((mv > d->dvfs_rail->max_millivolts) ||
			    (mv < d->dvfs_rail->min_millivolts) ||
			    (freq_idx && (mv < millivolts[freq_idx - 1]))) {
				WARN(1, "%s: invalid thermal dvfs entry %d(%d, %d)\n",
				     d->clk_name, mv, freq_idx, therm_idx);
				return -EINVAL;
			}
			if (mv > peak_voltages[freq_idx])
				peak_voltages[freq_idx] = mv;
		}
	}

	d->millivolts = therm_voltages;
	d->peak_millivolts = peak_voltages;
	d->therm_dvfs = ranges_num > 1;

	return 0;
}

static int tegra_dvfs_pm_notifier_event(struct notifier_block *nb,
				unsigned long event, void *data)
{
	if (event == PM_SUSPEND_PREPARE) {
		if (tegra_dvfs_suspend())
			return NOTIFY_STOP;
		pr_info("tegra_dvfs: suspended\n");
	} else if (event == PM_POST_SUSPEND) {
		tegra_dvfs_resume();
		pr_info("tegra_dvfs: resumed\n");
	}
	return NOTIFY_OK;
};

static struct notifier_block tegra_dvfs_pm_nb = {
	.notifier_call = tegra_dvfs_pm_notifier_event,
	.priority = -1,
};

static int tegra_dvfs_reboot_notify(struct notifier_block *nb,
				unsigned long event, void *data)
{
	switch (event) {
	case SYS_RESTART:
	case SYS_HALT:
	case SYS_POWER_OFF:
		tegra_dvfs_suspend();
		return NOTIFY_OK;
	}
	return NOTIFY_DONE;
}

static struct notifier_block tegra_dvfs_reboot_nb = {
	.notifier_call = tegra_dvfs_reboot_notify,
};

static void __tegra_dvfs_rail_disable(struct dvfs_rail *rail)
{
	int ret = -EPERM;
	int mv;

	if (rail->dfll_mode) {
		rail->disabled = true;
		return;
	}

	mv = tegra_dvfs_rail_get_disable_level(rail);
	mv = dvfs_rail_apply_limits(rail, mv, false);

	if (mv >= rail->millivolts)
		ret = dvfs_rail_set_voltage(rail, mv);
	if (ret) {
		pr_err("tegra_dvfs: failed to disable %s at %d\n",
		       rail->reg_id, rail->millivolts);
		return;
	}
	rail->disabled = true;
}

static void __tegra_dvfs_rail_enable(struct dvfs_rail *rail)
{
	rail->disabled = false;
	dvfs_rail_update(rail);
}

void tegra_dvfs_rail_enable(struct dvfs_rail *rail)
{
	if (!rail)
		return;

	mutex_lock(&dvfs_lock);

	if (rail->disabled)
		__tegra_dvfs_rail_enable(rail);

	mutex_unlock(&dvfs_lock);
}

void tegra_dvfs_rail_disable(struct dvfs_rail *rail)
{
	if (!rail)
		return;

	mutex_lock(&dvfs_lock);
	if (rail->disabled)
		goto out;

	__tegra_dvfs_rail_disable(rail);
out:
	mutex_unlock(&dvfs_lock);
}

bool tegra_dvfs_is_dfll_range(struct clk *c, unsigned long rate)
{
	struct dvfs *d;

	d = tegra_clk_to_dvfs(c);
	if (d == NULL) {
		pr_err("Failed to get dvfs structure\n");
		return false;
	}

	return dvfs_is_dfll_range(d, rate);
}
EXPORT_SYMBOL(tegra_dvfs_is_dfll_range);

int tegra_dvfs_set_dfll_range(struct clk *c, int range)
{
	struct dvfs *d;
	int ret = -EINVAL;

	mutex_lock(&dvfs_lock);
	d = tegra_clk_to_dvfs(c);
	if (d == NULL) {
		pr_err("Failed to get dvfs structure\n");
		goto out;
	}

	if (!d->dfll_millivolts)
		goto out;

	if ((range < DFLL_RANGE_NONE) || (range > DFLL_RANGE_HIGH_RATES))
		goto out;

	d->range = range;
	ret = 0;
out:
	mutex_unlock(&dvfs_lock);
	return ret;
}
EXPORT_SYMBOL(tegra_dvfs_set_dfll_range);

int tegra_dvfs_dfll_mode_set(struct clk *c, unsigned long rate)
{
	struct dvfs *d;

	mutex_lock(&dvfs_lock);

	d = tegra_clk_to_dvfs(c);
	if (d == NULL) {
		pr_err("Failed to get dvfs structure\n");
		mutex_unlock(&dvfs_lock);
		return -EINVAL;
	}

	if (!d->dvfs_rail->dfll_mode) {
		d->dvfs_rail->dfll_mode = true;
		__tegra_dvfs_set_rate(d, rate);
	}

	mutex_unlock(&dvfs_lock);

	return 0;
}
EXPORT_SYMBOL(tegra_dvfs_dfll_mode_set);

int tegra_dvfs_dfll_mode_clear(struct clk *c, unsigned long rate)
{
	int ret = 0;
	struct dvfs *d;

	mutex_lock(&dvfs_lock);

	d = tegra_clk_to_dvfs(c);
	if (d == NULL) {
		pr_err("Failed to get dvfs structure\n");
		mutex_unlock(&dvfs_lock);
		return -EINVAL;
	}

	if (d->dvfs_rail->dfll_mode) {
		d->dvfs_rail->dfll_mode = false;
		d->dvfs_rail->millivolts = regulator_get_voltage(
				d->dvfs_rail->reg) / 1000;
		if (d->dvfs_rail->disabled) {
			d->dvfs_rail->disabled = false;
			__tegra_dvfs_rail_disable(d->dvfs_rail);
		}
		ret = __tegra_dvfs_set_rate(d, rate);
	}

	mutex_unlock(&dvfs_lock);

	return ret;
}
EXPORT_SYMBOL(tegra_dvfs_dfll_mode_clear);

int tegra_dvfs_get_dfll_threshold(struct clk *c, unsigned long *rate)
{
	struct dvfs *d;

	d = tegra_clk_to_dvfs(c);
	if (d == NULL) {
		pr_err("Failed to get dvfs structure\n");
		return -EINVAL;
	}

	if (d->dvfs_rail && d->use_dfll_rate_min)
		*rate = d->use_dfll_rate_min;

	return 0;
}
EXPORT_SYMBOL(tegra_dvfs_get_dfll_threshold);

int tegra_dvfs_core_count_thermal_states(enum tegra_dvfs_core_thermal_type type)
{
	if (IS_ERR_OR_NULL(tegra_core_rail) || !tegra_core_rail->is_ready)
		return -EINVAL;

	if (type == TEGRA_DVFS_CORE_THERMAL_FLOOR)
		return tegra_core_rail->therm_floors_size;
	else if (type == TEGRA_DVFS_CORE_THERMAL_CAP)
		return tegra_core_rail->therm_caps_size;
	else
		return -EINVAL;
}
EXPORT_SYMBOL(tegra_dvfs_core_count_thermal_states);

int tegra_dvfs_core_get_thermal_index(enum tegra_dvfs_core_thermal_type type)
{
	if (IS_ERR_OR_NULL(tegra_core_rail) || !tegra_core_rail->is_ready)
		return -EINVAL;

	if (type == TEGRA_DVFS_CORE_THERMAL_FLOOR)
		return tegra_core_rail->therm_floor_idx;
	else if (type == TEGRA_DVFS_CORE_THERMAL_CAP)
		return tegra_core_rail->therm_cap_idx;
	else
		return -EINVAL;
}
EXPORT_SYMBOL(tegra_dvfs_core_get_thermal_index);

int tegra_dvfs_core_update_thermal_index(enum tegra_dvfs_core_thermal_type type,
					 unsigned long new_idx)
{
	struct dvfs_rail *rail = tegra_core_rail;
	int ret = 0;

	if (IS_ERR_OR_NULL(tegra_core_rail) || !tegra_core_rail->is_ready)
		return -EINVAL;

	mutex_lock(&dvfs_lock);
	if (type == TEGRA_DVFS_CORE_THERMAL_FLOOR) {
		if (rail->therm_floor_idx != new_idx) {
			rail->therm_floor_idx = new_idx;
			dvfs_rail_update(rail);
		}
	} else if (type == TEGRA_DVFS_CORE_THERMAL_CAP) {
		if (rail->therm_cap_idx != new_idx) {
			rail->therm_cap_idx = new_idx;
			dvfs_rail_update(rail);
		}
	} else {
		ret = -EINVAL;
	}
	mutex_unlock(&dvfs_lock);

	return ret;
}
EXPORT_SYMBOL(tegra_dvfs_core_update_thermal_index);

int tegra_dvfs_core_set_thermal_cap(struct clk *cap_clk,
				    unsigned long thermal_index)
{
	int mv;
	unsigned long rate = UINT_MAX;
	struct dvfs_rail *rail = tegra_core_rail;

	if (IS_ERR_OR_NULL(tegra_core_rail) || !tegra_core_rail->is_ready) {
		pr_err("tegra_dvfs: not ready to set thermal cap on %s\n",
		       cap_clk ? __clk_get_name(cap_clk) : "Unknown");
		return -EINVAL;
	}

	if (rail->therm_caps && thermal_index) {
		mv = rail->therm_caps[thermal_index - 1].mv;
		rate = tegra_dvfs_predict_hz_at_mv_max_tfloor(cap_clk, mv);
		if (IS_ERR_VALUE(rate)) {
			pr_err("tegra_dvfs: failed to get %s rate @ %dmV\n",
			       __clk_get_name(cap_clk), mv);
			return -EINVAL;
		}
	}
	pr_debug("tegra_dvfs: Set %lu on %s\n", rate, __clk_get_name(cap_clk));

	if (clk_set_rate(cap_clk, rate)) {
		pr_err("tegra_dvfs: failed to set cap rate %lu on %s\n",
		       rate, __clk_get_name(cap_clk));
		return -EINVAL;
	}

	return 0;
}
EXPORT_SYMBOL(tegra_dvfs_core_set_thermal_cap);

struct dvfs_rail *tegra_dvfs_get_rail_by_name(char *name)
{
	struct dvfs_rail *rail;

	list_for_each_entry(rail, &dvfs_rail_list, node) {
		if (!strcmp(rail->reg_id, name))
			return rail;
	}

	return NULL;
}
EXPORT_SYMBOL(tegra_dvfs_get_rail_by_name);

bool tegra_dvfs_is_rail_up(struct dvfs_rail *rail)
{
	bool ret = false;

	if (!rail)
		return false;

	if (!rail->in_band_pm)
		return true;

	mutex_lock(&dvfs_lock);
	if (rail->reg)
		ret = regulator_is_enabled(rail->reg) > 0;
	mutex_unlock(&dvfs_lock);
	return ret;
}
EXPORT_SYMBOL(tegra_dvfs_is_rail_up);

int tegra_dvfs_rail_power_up(struct dvfs_rail *rail)
{
	int ret = -ENOENT;

	if (!rail || !rail->in_band_pm)
		return -EINVAL;

	mutex_lock(&dvfs_lock);
	if (rail->reg) {
		ret = regulator_enable(rail->reg);
		if (!ret && !timekeeping_suspended) {
			rail->stats.off = false;
			dvfs_rail_stats_update(rail, rail->millivolts,
					       ktime_get());
		}
	}
	mutex_unlock(&dvfs_lock);
	return ret;
}
EXPORT_SYMBOL(tegra_dvfs_rail_power_up);

int tegra_dvfs_rail_power_down(struct dvfs_rail *rail)
{
	int ret = -ENOENT;

	if (!rail || !rail->in_band_pm)
		return -EINVAL;

	mutex_lock(&dvfs_lock);
	if (rail->reg) {
		ret = regulator_disable(rail->reg);
		if (!ret && !timekeeping_suspended) {
			dvfs_rail_stats_update(rail, 0, ktime_get());
			rail->stats.off = true;

		}
	}
	mutex_unlock(&dvfs_lock);
	return ret;
}
EXPORT_SYMBOL(tegra_dvfs_rail_power_down);

unsigned long tegra_dvfs_get_fmax_at_vmin_safe_t(struct clk *c)
{
	struct dvfs *d = tegra_clk_to_dvfs(c);

	if (!d)
		return 0;

	return d->fmax_at_vmin_safe_t;
}
EXPORT_SYMBOL(tegra_dvfs_get_fmax_at_vmin_safe_t);

bool tegra_dvfs_is_rail_ready(struct dvfs_rail *rail)
{
	return rail->is_ready;
}
EXPORT_SYMBOL(tegra_dvfs_is_rail_ready);

/*
 * Validate rail thermal floors/caps, and get its size.
 * Valid floors/caps:
 * - voltage limits are descending with temperature increasing.
 * - the lowest limit is above rail minimum voltage in pll and
 *   in dfll mode (if applicable).
 * - the highest limit is below rail nominal voltage.
 */
static int get_thermal_limits_size(struct dvfs_rail *rail,
				   enum tegra_dvfs_core_thermal_type type)
{
	const struct dvfs_therm_limits *limits;
	int i;

	if (type == TEGRA_DVFS_CORE_THERMAL_FLOOR)
		limits = rail->therm_floors;
	else if (type == TEGRA_DVFS_CORE_THERMAL_CAP)
		limits = rail->therm_caps;
	else
		return -EINVAL;

	if (!limits[0].mv) {
		pr_warn("%s: Missing thermal limits\n", rail->reg_id);
		return -EINVAL;
	}

	for (i = 0; i < MAX_THERMAL_LIMITS - 1; i++) {
		if (!limits[i + 1].mv)
			break;

		if ((limits[i].temperature >= limits[i + 1].temperature) ||
		    (limits[i].mv < limits[i + 1].mv)) {
			pr_warn("%s: Unordered thermal limits\n",
				rail->reg_id);
			return -EINVAL;
		}
	}

	if (limits[i].mv < rail->min_millivolts) {
		pr_warn("%s: Thermal floors below minimum voltage\n",
			rail->reg_id);
		return -EINVAL;
	}

	return i + 1;
}

void tegra_dvfs_core_init_therm_limits(struct dvfs_rail *rail)
{
	int size;

	size = get_thermal_limits_size(rail, TEGRA_DVFS_CORE_THERMAL_FLOOR);
	if (size <= 0 || rail->therm_floors[0].mv > rail->nominal_millivolts) {
		rail->therm_floors = NULL;
		rail->therm_floors_size = 0;
		pr_warn("%s: invalid Vmin thermal floors\n", rail->reg_id);
	} else {
		rail->therm_floors_size = size;
		rail->therm_floor_idx = 0;
	}

	size = get_thermal_limits_size(rail, TEGRA_DVFS_CORE_THERMAL_CAP);
	if (size <= 0) {
		rail->therm_caps = NULL;
		rail->therm_caps_size = 0;
		pr_warn("%s: invalid Vmax thermal caps\n", rail->reg_id);
	} else {
		rail->therm_caps_size = size;
		/*
		 * Core voltage and module rates are not throttled on boot until
		 * CORE_CDEV_TYPE_CAP is registered. In fact, core boot voltage
		 * is allowed above high temperature cap during boot.
		 */
		rail->therm_cap_idx = 0;
	}
}

static int tegra_config_dvfs(struct dvfs_rail *rail)
{
	int i;
	struct dvfs *d;

	list_for_each_entry(d, &rail->dvfs, reg_node) {
		if (__clk_is_enabled(d->clk) || __clk_is_prepared(d->clk)) {
			d->cur_rate = clk_get_rate(d->clk);
			d->cur_millivolts = d->max_millivolts;

			for (i = 0; i < d->num_freqs; i++)
				if (d->cur_rate <= d->freqs[i])
					break;

			if (i != d->num_freqs)
				d->cur_millivolts = d->millivolts[i];
		}

		mutex_unlock(&dvfs_lock);
		clk_notifier_register(d->clk, &tegra_dvfs_nb);
		mutex_lock(&dvfs_lock);
	}

	return 0;
}

static int tegra_dvfs_regulator_init(struct device *dev)
{
	struct dvfs_rail *rail;
	int err;

	mutex_lock(&dvfs_lock);

	list_for_each_entry(rail, &dvfs_rail_list, node) {
		err = dvfs_rail_connect_to_regulator(dev, rail);
		if (err) {
			if (!rail->disabled)
				__tegra_dvfs_rail_disable(rail);

			mutex_unlock(&dvfs_lock);
			return err;
		}
	}

	list_for_each_entry(rail, &dvfs_rail_list, node) {
		tegra_config_dvfs(rail);
		if (rail->disabled) {
			/* Overwrite boot voltage with nominal */
			rail->disabled = false;
			__tegra_dvfs_rail_disable(rail);
		} else {
			__tegra_dvfs_rail_enable(rail);  /* update to clks */
		}
	}

	core_dvfs_started = true;

	mutex_unlock(&dvfs_lock);

	register_pm_notifier(&tegra_dvfs_pm_nb);
	register_reboot_notifier(&tegra_dvfs_reboot_nb);

	return 0;
}

static int tegra_vts_get_max_state(struct thermal_cooling_device *cdev,
					unsigned long *max_state)
{
	struct dvfs_rail *rail = cdev->devdata;

	*max_state = rail->vts_number_of_trips;

	return 0;
}

static int tegra_vts_get_cur_state(struct thermal_cooling_device *cdev,
					unsigned long *cur_state)
{
	struct dvfs_rail *rail = cdev->devdata;

	*cur_state = rail->therm_scale_idx;

	return 0;
}

static int tegra_vts_set_cur_state(struct thermal_cooling_device *cdev,
					 unsigned long cur_state)
{
	struct dvfs_rail *rail = cdev->devdata;
	struct dvfs *d, *first;
	int ret = 0;

	mutex_lock(&dvfs_lock);

	if (rail->therm_scale_idx == cur_state)
		goto out;

	rail->therm_scale_idx = cur_state;

	first = list_first_entry(&rail->dvfs, struct dvfs, reg_node);
	if (first->therm_dvfs && first->na_dvfs && first->cur_rate) {
		/* only GPU thermal DVFS can be noise aware and this
		 * rail has only a single clock. Therefor we can just
		 * update the NA DVFS config by doing calling
		 * clk_set_rate_refresh and leave the normal DVFS notifier
		 * to handle the voltage update.
		 */
		mutex_unlock(&dvfs_lock);
		return clk_set_rate_refresh(first->clk);
	} else if ((!first->therm_dvfs || !first->na_dvfs) &&
			first->dvfs_rail) {
		list_for_each_entry(d, &rail->dvfs, reg_node) {
			if (d->therm_dvfs)
				d->cur_millivolts =
				predict_mv_at_hz_cur_tfloor(d, d->cur_rate);
		}
		ret = dvfs_rail_update(first->dvfs_rail);
	}

out:
	mutex_unlock(&dvfs_lock);

	return ret;
}

static struct thermal_cooling_device_ops tegra_vts_cooling_ops = {
	.get_max_state = tegra_vts_get_max_state,
	.get_cur_state = tegra_vts_get_cur_state,
	.set_cur_state = tegra_vts_set_cur_state,
};

#ifdef CONFIG_DEBUG_FS

/* To emulate and show rail relations with 0 mV on dependent rail-to */
static struct dvfs_rail show_to;
static struct dvfs_relationship show_rel;

static int dvfs_tree_show(struct seq_file *s, void *data)
{
	struct dvfs *d;
	struct dvfs_rail *rail;
	struct dvfs_relationship *rel;
	int cur_max_millivolts = INT_MIN;
	int num_clks;

	seq_puts(s, "   clock           rate       mV\n");
	seq_puts(s, "-------------------------------------\n");

	mutex_lock(&dvfs_lock);

	list_for_each_entry(rail, &dvfs_rail_list, node) {
		int therm_mv = 0;

		seq_printf(s, "%s %d mV%s%s:\n", rail->reg_id, rail->millivolts,
			   rail->stats.off ? " OFF" : " ON",
			   rail->dfll_mode ? " dfll mode" :
				rail->disabled ? " disabled" : "");
		list_for_each_entry(rel, &rail->relationships_from, from_node) {
			show_rel = *rel;
			show_rel.to = &show_to;
			show_to = *rel->to;
			show_to.millivolts = show_to.new_millivolts = 0;
			seq_printf(s, "   %-10s %-7d mV %-4d mV .. %-4d mV\n",
				rel->from->reg_id, rel->from->millivolts,
				dvfs_solve_relationship(&show_rel),
				dvfs_solve_relationship(rel));
		}
		seq_printf(s, "   %-26s %-4d mV\n", "nominal",
			   rail->nominal_millivolts);
		seq_printf(s, "   %-26s %-4d mV\n", "minimum",
			   rail->min_millivolts);
		seq_printf(s, "   %-26s %-4d mV\n", "offset", rail->dbg_mv_offs);
		seq_printf(s, "   %-26s %-4d mV\n", "override",
			   rail->override_millivolts);

		if (rail->dfll_mode) {
			therm_mv = tegra_dfll_get_thermal_floor_mv();
		} else if ((rail->therm_floors) &&
			   (rail->therm_floor_idx < rail->therm_floors_size)) {
			therm_mv = rail->therm_floors[rail->therm_floor_idx].mv;
		}
		seq_printf(s, "   %-26s %-4d mV\n", "therm_floor", therm_mv);

		therm_mv = 0;
		if (rail->dfll_mode) {
			therm_mv = tegra_dfll_get_thermal_cap_mv();
		} else if ((rail->therm_caps) &&
			   (rail->therm_cap_idx > 0)) {
			therm_mv = rail->therm_caps[rail->therm_cap_idx - 1].mv;
		}
		seq_printf(s, "   %-26s %-4d mV\n", "therm_cap", therm_mv);

		num_clks = 0;
		list_for_each_entry(d, &rail->dvfs, reg_node) {
			num_clks++;
			if (d->cur_millivolts > cur_max_millivolts)
				cur_max_millivolts = d->cur_millivolts;
		}

		while (num_clks > 0) {
			int next_max_millivolts = INT_MIN;

			list_for_each_entry(d, &rail->dvfs, reg_node) {
				if (d->cur_millivolts > next_max_millivolts &&
				    d->cur_millivolts < cur_max_millivolts)
					next_max_millivolts = d->cur_millivolts;

				if (d->cur_millivolts != cur_max_millivolts)
					continue;

				seq_printf(s, "   %-15s %-10lu %-4d mV\n",
					d->clk_name, d->cur_rate,
					d->cur_millivolts);
				num_clks--;
				WARN_ON(num_clks < 0);
			}
			cur_max_millivolts = next_max_millivolts;
		}
	}

	mutex_unlock(&dvfs_lock);

	return 0;
}

static int dvfs_tree_open(struct inode *inode, struct file *file)
{
	return single_open(file, dvfs_tree_show, inode->i_private);
}

static const struct file_operations dvfs_tree_fops = {
	.open		= dvfs_tree_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
};

static int dvfs_table_show(struct seq_file *s, void *data)
{
	int i;
	struct dvfs *d;
	struct dvfs_rail *rail;
	const int *v_pll, *last_v_pll = NULL;
	const int *v_dfll, *last_v_dfll = NULL;

	seq_puts(s, "DVFS tables: units mV/MHz\n");

	mutex_lock(&dvfs_lock);

	list_for_each_entry(rail, &dvfs_rail_list, node) {
		seq_printf(s, "%-8s table version: %s\n",
			   rail->reg_id, rail->nvver ? : "N/A");
	}

	list_for_each_entry(rail, &dvfs_rail_list, node) {
		list_for_each_entry(d, &rail->dvfs, reg_node) {
			bool mv_done = false;
			v_pll =  dvfs_get_millivolts_pll(d);
			v_dfll = d->dfll_millivolts;

			if (v_pll && (last_v_pll != v_pll)) {
				if (!mv_done) {
					seq_puts(s, "\n");
					mv_done = true;
				}
				last_v_pll = v_pll;
				seq_printf(s, "%-16s", rail->reg_id);
				for (i = 0; i < d->num_freqs; i++)
					seq_printf(s, "%7d", v_pll[i]);
				seq_puts(s, "\n");
			}

			if (v_dfll && (last_v_dfll != v_dfll)) {
				if (!mv_done) {
					seq_puts(s, "\n");
					mv_done = true;
				}
				last_v_dfll = v_dfll;
				seq_printf(s, "%-8s (dfll) ", rail->reg_id);
				for (i = 0; i < d->num_freqs; i++)
					seq_printf(s, "%7d", v_dfll[i]);
				seq_puts(s, "\n");
			}

			seq_printf(s, "%-16s", d->clk_name);
			for (i = 0; i < d->num_freqs; i++) {
				unsigned int f = d->freqs[i]/100000;
				seq_printf(s, " %4u.%u", f/10, f%10);
			}
			if (d->alt_freqs) {
				seq_puts(s, "\n");
				seq_printf(s, "%-10s (alt)", d->clk_name);
				for (i = 0; i < d->num_freqs; i++) {
					unsigned int f = d->alt_freqs[i]/100000;
					seq_printf(s, " %4u.%u", f/10, f%10);
				}
			}

			seq_puts(s, "\n");
		}
	}

	mutex_unlock(&dvfs_lock);

	return 0;
}

static int dvfs_table_open(struct inode *inode, struct file *file)
{
	return single_open(file, dvfs_table_show, inode->i_private);
}

static const struct file_operations dvfs_table_fops = {
	.open		= dvfs_table_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
};

static int rail_stats_save_to_buf(char *buf, int len)
{
	int i;
	struct dvfs_rail *rail;
	char *str = buf;
	char *end = buf + len;

	str += scnprintf(str, end - str, "%-12s %-10s\n", "millivolts", "time");

	mutex_lock(&dvfs_lock);

	list_for_each_entry(rail, &dvfs_rail_list, node) {
		str += scnprintf(str, end - str, "%s (bin: %d.%dmV)\n",
			   rail->reg_id,
			   rail->stats.bin_uv / 1000,
			   (rail->stats.bin_uv / 10) % 100);

		dvfs_rail_stats_update(rail, -1, ktime_get());

		str += scnprintf(str, end - str, "%-12d %-10llu\n", 0,
			cputime64_to_clock_t(msecs_to_jiffies(
				ktime_to_ms(rail->stats.time_at_mv[0]))));

		for (i = 1; i <= DVFS_RAIL_STATS_TOP_BIN; i++) {
			ktime_t ktime_zero = ktime_set(0, 0);
			if (ktime_equal(rail->stats.time_at_mv[i], ktime_zero))
				continue;
			str += scnprintf(str, end - str, "%-12d %-10llu\n",
				rail->min_millivolts +
				(i - 1) * rail->stats.bin_uv / 1000,
				cputime64_to_clock_t(msecs_to_jiffies(
					ktime_to_ms(rail->stats.time_at_mv[i])))
			);
		}
	}
	mutex_unlock(&dvfs_lock);
	return str - buf;
}

static int rail_stats_show(struct seq_file *s, void *data)
{
	char *buf = kzalloc(PAGE_SIZE, GFP_KERNEL);
	int size = 0;

	if (!buf)
		return -ENOMEM;

	size = rail_stats_save_to_buf(buf, PAGE_SIZE);
	seq_write(s, buf, size);
	kfree(buf);
	return 0;
}

static int rail_stats_open(struct inode *inode, struct file *file)
{
	return single_open(file, rail_stats_show, inode->i_private);
}

static const struct file_operations rail_stats_fops = {
	.open		= rail_stats_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
};

static int gpu_dvfs_t_show(struct seq_file *s, void *data)
{
	int i, j;
	int num_ranges = 1;
	int *trips = NULL;
	struct dvfs *d;
	struct dvfs_rail *rail = tegra_gpu_rail;
	int max_mv[MAX_DVFS_FREQS] = {};

	if (!tegra_gpu_rail) {
		seq_printf(s, "Only supported for T124 or higher\n");
		return -ENOSYS;
	}

	mutex_lock(&dvfs_lock);

	d = list_first_entry(&rail->dvfs, struct dvfs, reg_node);
	if (rail->vts_cdev && d->therm_dvfs) {
		num_ranges = rail->vts_number_of_trips + 1;
		trips = rail->vts_trips_table;
	}

	seq_printf(s, "%-11s", "T(C)\\F(kHz)");
	for (i = 0; i < d->num_freqs; i++) {
		unsigned int f = d->freqs[i]/1000;
		seq_printf(s, " %7u", f);
	}
	seq_printf(s, "\n");

	for (j = 0; j < num_ranges; j++) {
		seq_printf(s, "%s", j == rail->therm_scale_idx ? ">" : " ");

		if (!trips || (num_ranges == 1))
			seq_printf(s, "%4s..%-4s", "", "");
		else if (j == 0)
			seq_printf(s, "%4s..%-4d", "", trips[j]);
		else if (j == num_ranges - 1)
			seq_printf(s, "%4d..%-4s", trips[j], "");
		else
			seq_printf(s, "%4d..%-4d", trips[j-1], trips[j]);

		for (i = 0; i < d->num_freqs; i++) {
			int mv = *(d->millivolts + j * MAX_DVFS_FREQS + i);
			seq_printf(s, " %7d", mv);
			max_mv[i] = max(max_mv[i], mv);
		}
		seq_printf(s, " mV\n");
	}

	seq_printf(s, "%3s%-8s\n", "", "------");
	seq_printf(s, "%3s%-8s", "", "max(T)");
	for (i = 0; i < d->num_freqs; i++)
		seq_printf(s, " %7d", max_mv[i]);
	seq_printf(s, " mV\n");

	mutex_unlock(&dvfs_lock);

	return 0;
}

static int gpu_dvfs_t_open(struct inode *inode, struct file *file)
{
	return single_open(file, gpu_dvfs_t_show, NULL);
}

static const struct file_operations gpu_dvfs_t_fops = {
	.open           = gpu_dvfs_t_open,
	.read           = seq_read,
	.llseek         = seq_lseek,
	.release        = single_release,
};

static int dvfs_offset_get(void *data, u64 *val)
{
	struct dvfs_rail *rail = data;

	*val = rail->dbg_mv_offs;

	return 0;
}

static int dvfs_offset_set(void *data, u64 val)
{
	struct dvfs_rail *rail = data;

	mutex_lock(&dvfs_lock);

	rail->dbg_mv_offs = val;
	dvfs_rail_update(rail);

	mutex_unlock(&dvfs_lock);

	return 0;
}
DEFINE_SIMPLE_ATTRIBUTE(dvfs_offset_fops, dvfs_offset_get, dvfs_offset_set, "%lld\n");

static int dvfs_override_get(void *data, u64 *val)
{
	struct dvfs_rail *rail = data;

	*val = rail->override_millivolts;

	return 0;
}

static int dvfs_override_set(void *data, u64 val)
{
	struct dvfs_rail *rail = data;

	mutex_lock(&dvfs_lock);

	rail->override_millivolts = val;
	dvfs_rail_update(rail);

	mutex_unlock(&dvfs_lock);

	return 0;
}
DEFINE_SIMPLE_ATTRIBUTE(dvfs_override_fops,
			dvfs_override_get, dvfs_override_set, "%lld\n");

static int dvfs_debugfs_init(void)
{
	struct dentry *d_root, *d;

	d_root = debugfs_create_dir("tegra_dvfs", NULL);
	if (!d_root)
		return -ENOMEM;

	d = debugfs_create_file("dvfs", S_IRUGO, d_root, NULL,
		&dvfs_tree_fops);
	if (!d)
		return -ENOMEM;

	d = debugfs_create_file("dvfs_table", S_IRUGO, d_root, NULL,
		&dvfs_table_fops);
	if (!d)
		return -ENOMEM;

	d = debugfs_create_file("rails", S_IRUGO, d_root, NULL,
		&rail_stats_fops);
	if (!d)
		return -ENOMEM;

	d = debugfs_create_file("gpu_dvfs_t", S_IRUGO, d_root, NULL,
				&gpu_dvfs_t_fops);
	if (!d)
		return -ENOMEM;

	d = debugfs_create_file("vdd_core_offs", S_IRUGO | S_IWUSR, d_root,
				tegra_core_rail,  &dvfs_offset_fops);
	if (!d)
		return -ENOMEM;

	d = debugfs_create_file("vdd_gpu_offs", S_IRUGO | S_IWUSR, d_root,
				tegra_gpu_rail,  &dvfs_offset_fops);
	if (!d)
		return -ENOMEM;

	d = debugfs_create_file("vdd_core_override", S_IRUGO | S_IWUSR, d_root,
				tegra_core_rail,  &dvfs_override_fops);
	if (!d)
		return -ENOMEM;

	d = debugfs_create_file("vdd_gpu_override", S_IRUGO | S_IWUSR, d_root,
				tegra_gpu_rail,  &dvfs_override_fops);
	if (!d)
		return -ENOMEM;

	return 0;
}

#endif

typedef int (*dvfs_init_cb_t)(struct device *);

static const struct of_device_id tegra_dvfs_of_match[] = {
	{ .compatible = "nvidia,tegra124-dvfs", .data = tegra124_init_dvfs },
	{ .compatible = "nvidia,tegra210-dvfs", .data = tegra210_init_dvfs },
	{ .compatible = "nvidia,tegra210b01-dvfs",
	  .data = tegra210b01_init_dvfs },
	{},
};

static int tegra_dvfs_probe(struct platform_device *pdev)
{
	const struct of_device_id *match;
	dvfs_init_cb_t dvfs_init_cb;
	struct dvfs_rail *rail;
	int ret = -EINVAL;

	match = of_match_node(tegra_dvfs_of_match, pdev->dev.of_node);
	if (!match)
		goto out;

	dvfs_init_cb = (dvfs_init_cb_t)match->data;
	ret = dvfs_init_cb(&pdev->dev);
	if (ret)
		goto out;

	ret = tegra_dvfs_regulator_init(&pdev->dev);
	if (ret)
		goto out;

	list_for_each_entry(rail, &dvfs_rail_list, node) {
		rail->is_ready = true;
		if (rail->vts_of_node) {
			char *name;

			name = kasprintf(GFP_KERNEL, "%s-vts", rail->reg_id);
			rail->vts_cdev = thermal_of_cooling_device_register(
				rail->vts_of_node, name, rail,
				&tegra_vts_cooling_ops);
			pr_info("tegra_dvfs: %s: %sregistered\n", name,
				IS_ERR_OR_NULL(rail->vts_cdev) ? "not " : "");
			kfree(name);
		}
	}

#ifdef CONFIG_DEBUG_FS
	dvfs_debugfs_init();
#endif
	return 0;
out:
	return ret;
}

static int tegra_dvfs_remove(struct platform_device *pdev)
{
	struct dvfs *d;

	core_dvfs_started = false;

	unregister_pm_notifier(&tegra_dvfs_reboot_nb);
	unregister_pm_notifier(&tegra_dvfs_pm_nb);

	list_for_each_entry(d, &tegra_core_rail->dvfs, reg_node) {
		clk_notifier_unregister(d->clk, &tegra_dvfs_nb);
	}

	return 0;
}

static struct platform_driver tegra_dvfs_platdrv = {
	.driver = {
		.name	= "tegra-dvfs",
		.owner	= THIS_MODULE,
		.of_match_table = tegra_dvfs_of_match,
	},
	.probe		= tegra_dvfs_probe,
	.remove		= tegra_dvfs_remove,
};

static int __init tegra_dvfs_platdrv_init(void)
{
	return platform_driver_register(&tegra_dvfs_platdrv);
}
subsys_initcall_sync(tegra_dvfs_platdrv_init);

static void __exit tegra_dvfs_platdrv_exit(void)
{
	platform_driver_unregister(&tegra_dvfs_platdrv);
}
module_exit(tegra_dvfs_platdrv_exit);
