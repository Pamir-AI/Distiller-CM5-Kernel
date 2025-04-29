/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * This header provides constants for Good Display epaper displays
 *
 * Copyright 2019 Jan Sebastian Goette
 */

#ifndef _DT_BINDINGS_DISPLAY_GDEPAPER_H
#define _DT_BINDINGS_DISPLAY_GDEPAPER_H

#define GDEP_CTRL_RES_320X300  0
#define GDEP_CTRL_RES_300X200  1
#define GDEP_CTRL_RES_296X160  2
#define GDEP_CTRL_RES_296X128  3

#define GDEPAPER_COL_BW        0
#define GDEPAPER_COL_BW_RED    1
#define GDEPAPER_COL_BW_YELLOW 2
#define GDEPAPER_COL_END       3

#define GDEP_PWR_VGHL_16V      0
#define GDEP_PWR_VGHL_15V      1
#define GDEP_PWR_VGHL_14V      2
#define GDEP_PWR_VGHL_13V      3

#endif /* _DT_BINDINGS_DISPLAY_GDEPAPER_H */
