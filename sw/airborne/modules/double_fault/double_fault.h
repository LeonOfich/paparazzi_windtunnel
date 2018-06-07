/*
 * Copyright (C) Leon Sijbers
 *
 * This file is part of paparazzi
 *
 * paparazzi is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 * paparazzi is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with paparazzi; see the file COPYING.  If not, see
 * <http://www.gnu.org/licenses/>.
 */
/**
 * @file "modules/double_fault/double_fault.h"
 * @author Leon Sijbers
 * Introduce a double (diagonal) rotor failure
 */

#ifndef DOUBLE_FAULT_H
#define DOUBLE_FAULT_H
#include "std.h"

bool double_fault_flag;
bool double_fault_running; 

extern void double_fault_init();
extern void double_fault();
extern bool double_fault_status();

#endif

