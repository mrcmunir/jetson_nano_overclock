/*
 * include/linux/platform_data/pwm_fan.h
 *
 * Copyright (c) 2013-2014, NVIDIA CORPORATION.  All rights reserved.
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

#ifndef _PWM_FAN_H_
#define _PWM_FAN_H_

#define MAX_ACTIVE_STATES 10

struct pwm_fan_platform_data {
	int active_steps;
	int active_rpm[MAX_ACTIVE_STATES];
	int active_pwm[MAX_ACTIVE_STATES];
	int active_rru[MAX_ACTIVE_STATES];
	int active_rrd[MAX_ACTIVE_STATES];
	int state_cap_lookup[MAX_ACTIVE_STATES];
	int pwm_period;
	int pwm_id;
	int step_time;
	int active_pwm_max;
	int state_cap;
	int tach_gpio;
	int pwm_gpio;
};

#if IS_BUILTIN(CONFIG_PWM_FAN)
bool is_fan_always_on(struct thermal_cooling_device *cdev);
#endif
#endif
