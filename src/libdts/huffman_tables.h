/*
 * Copyright (C) 2000-2003 the xine project
 *
 * This file is part of xine, a unix video player.
 *
 * xine is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * xine is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA
 *
 * $Id: huffman_tables.h,v 1.1 2003/08/05 11:30:56 jcdutton Exp $
 *
 * 04-08-2003 DTS software decode (C) James Courtier-Dutton
 *
 */

static int HuffA3[][2] = {
  {+2, 0} ,
  {-2, 2} ,
  {-1, -3}
};

static int32_t HuffA4[][2] = {
  {+4, 0}, /* Add +4 to all neg values below to get result. */
  {-4, 2}, /* 0 -> 0 */
  {-3, 3}, /* 10 -> 1 */
  {-2, -1} /* 110 -> 2, 111 -> 3 */
};

static int32_t HuffB4[][2] = {
  {+4, 0}, /* Add +4 to all neg values below to get result. */
  {-1, 2}, /* 0 -> 3 */
  {-4, 3}, /* 10 -> 0 */
  {-3, -2} /* 110 -> 1, 111 -> 2 */
};

static int32_t HuffC4[][2] = {
  {+4, 0}, /* Add +4 to all neg values below to get result. */
  {-2, 2}, /* 0 -> 2 */
  {-1, 3}, /* 10 -> 3 */
  {-4, -3} /* 110 -> 0, 111 -> 1 */
};

static int32_t HuffD4[][2] = {
  {+4, 0}, /* Add +4 to all neg values below to get result. */
  {2, 3},
  {-4, -3}, /* 00 -> 0 , 01 -> 1 */
  {-2, -1} /* 10 -> 2, 11 -> 3 */
};

