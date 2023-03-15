/*
 * include/linux/pid_thermal_gov.h
 *
 * Copyright (c) 2013, NVIDIA CORPORATION.  All rights reserved.
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
 *
 */

#ifndef _PID_THERMAL_GOV_H
#define _PID_THERMAL_GOV_H

struct pid_thermal_gov_params {
	int max_err_temp; /* max error temperature in mC */
	int max_err_gain; /* max error gain */

	int gain_p; /* proportional gain */
	int gain_d; /* derivative gain */

	/* max derivative output, percentage of max error */
	unsigned long max_dout;

	unsigned long up_compensation;
	unsigned long down_compensation;
};

#endif
