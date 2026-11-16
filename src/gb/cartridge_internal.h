/**
 * @file    cartridge_internal.h
 * @brief   Shared cartridge controller reset and approximate startup helpers.
 * @author  cupidthecat
 * @date    2026-11-16
 *
 * Copyright (C) 2026 cupidthecat
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <https://gnu.org>.
 */

#ifndef CUPID_GB_CARTRIDGE_INTERNAL_H
#define CUPID_GB_CARTRIDGE_INTERNAL_H

#include "cupid/gb/gb.h"

/* Physical header, layout and storage ownership must already be installed.
 * Controller reset does not erase cartridge storage or touch host callbacks. */
void cupid_gb_cartridge_reset_controls(CupidGb *gb);
void cupid_gb_cartridge_post_boot(CupidGb *gb);

#endif
