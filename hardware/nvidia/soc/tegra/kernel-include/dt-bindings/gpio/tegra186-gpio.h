/*
 * Copyright (c) 2016-2020, NVIDIA CORPORATION.  All rights reserved.
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

/*
 * This header provides constants for binding nvidia,tegra186-gpio*.
 *
 * The first cell in Tegra's GPIO specifier is the GPIO ID. The macros below
 * provide names for this.
 *
 * The second cell contains standard flag values specified in gpio.h.
 */

#ifndef _DT_BINDINGS_GPIO_TEGRA_GPIO_H
#define _DT_BINDINGS_GPIO_TEGRA_GPIO_H

#include <dt-bindings/gpio/gpio.h>

/* GPIOs implemented by main GPIO controller */
#define TEGRA_MAIN_GPIO_PORT_A 0
#define TEGRA_MAIN_GPIO_PORT_B 1
#define TEGRA_MAIN_GPIO_PORT_C 2
#define TEGRA_MAIN_GPIO_PORT_D 3
#define TEGRA_MAIN_GPIO_PORT_E 4
#define TEGRA_MAIN_GPIO_PORT_F 5
#define TEGRA_MAIN_GPIO_PORT_G 6
#define TEGRA_MAIN_GPIO_PORT_H 7
#define TEGRA_MAIN_GPIO_PORT_I 8
#define TEGRA_MAIN_GPIO_PORT_J 9
#define TEGRA_MAIN_GPIO_PORT_K 10
#define TEGRA_MAIN_GPIO_PORT_L 11
#define TEGRA_MAIN_GPIO_PORT_M 12
#define TEGRA_MAIN_GPIO_PORT_N 13
#define TEGRA_MAIN_GPIO_PORT_O 14
#define TEGRA_MAIN_GPIO_PORT_P 15
#define TEGRA_MAIN_GPIO_PORT_Q 16
#define TEGRA_MAIN_GPIO_PORT_R 17
#define TEGRA_MAIN_GPIO_PORT_T 18
#define TEGRA_MAIN_GPIO_PORT_X 19
#define TEGRA_MAIN_GPIO_PORT_Y 20
#define TEGRA_MAIN_GPIO_PORT_BB 21
#define TEGRA_MAIN_GPIO_PORT_CC 22
#define TEGRA_MAIN_GPIO_PORT_DD 23

#define TEGRA_MAIN_GPIO(port, offset) \
	((TEGRA_MAIN_GPIO_PORT_##port * 8) + offset)

/* GPIOs implemented by AON GPIO controller */
#define TEGRA_AON_GPIO_PORT_S 0
#define TEGRA_AON_GPIO_PORT_U 1
#define TEGRA_AON_GPIO_PORT_V 2
#define TEGRA_AON_GPIO_PORT_W 3
#define TEGRA_AON_GPIO_PORT_Z 4
#define TEGRA_AON_GPIO_PORT_AA 5
#define TEGRA_AON_GPIO_PORT_EE 6
#define TEGRA_AON_GPIO_PORT_FF 7

#define TEGRA_AON_GPIO(port, offset) \
	((TEGRA_AON_GPIO_PORT_##port * 8) + offset)

/* All pins */
#define TEGRA_PIN_BASE_ID_A 0
#define TEGRA_PIN_BASE_ID_B 1
#define TEGRA_PIN_BASE_ID_C 2
#define TEGRA_PIN_BASE_ID_D 3
#define TEGRA_PIN_BASE_ID_E 4
#define TEGRA_PIN_BASE_ID_F 5
#define TEGRA_PIN_BASE_ID_G 6
#define TEGRA_PIN_BASE_ID_H 7
#define TEGRA_PIN_BASE_ID_I 8
#define TEGRA_PIN_BASE_ID_J 9
#define TEGRA_PIN_BASE_ID_K 10
#define TEGRA_PIN_BASE_ID_L 11
#define TEGRA_PIN_BASE_ID_M 12
#define TEGRA_PIN_BASE_ID_N 13
#define TEGRA_PIN_BASE_ID_O 14
#define TEGRA_PIN_BASE_ID_P 15
#define TEGRA_PIN_BASE_ID_Q 16
#define TEGRA_PIN_BASE_ID_R 17
#define TEGRA_PIN_BASE_ID_S 18
#define TEGRA_PIN_BASE_ID_T 19
#define TEGRA_PIN_BASE_ID_U 20
#define TEGRA_PIN_BASE_ID_V 21
#define TEGRA_PIN_BASE_ID_W 22
#define TEGRA_PIN_BASE_ID_X 23
#define TEGRA_PIN_BASE_ID_Y 24
#define TEGRA_PIN_BASE_ID_Z 25
#define TEGRA_PIN_BASE_ID_AA 26
#define TEGRA_PIN_BASE_ID_BB 27
#define TEGRA_PIN_BASE_ID_CC 28
#define TEGRA_PIN_BASE_ID_DD 29
#define TEGRA_PIN_BASE_ID_EE 30
#define TEGRA_PIN_BASE_ID_FF 31

#define TEGRA_PIN_BASE(port) (TEGRA_PIN_BASE_ID_##port * 8)

#define TEGRA_MAIN_GPIO_RANGE(st, end) \
	((TEGRA_MAIN_GPIO_PORT_##end - TEGRA_MAIN_GPIO_PORT_##st + 1) * 8)
#define TEGRA_MAIN_GPIO_BASE(port) (TEGRA_MAIN_GPIO_PORT_##port * 8)

#define TEGRA_AON_GPIO_RANGE(st, end) \
	((TEGRA_AON_GPIO_PORT_##end - TEGRA_AON_GPIO_PORT_##st + 1) * 8)
#define TEGRA_AON_GPIO_BASE(port) (TEGRA_AON_GPIO_PORT_##port * 8)

#endif
