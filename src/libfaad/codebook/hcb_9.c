/*
** FAAD - Freeware Advanced Audio Decoder
** Copyright (C) 2002 M. Bakker
**
** This program is free software; you can redistribute it and/or modify
** it under the terms of the GNU General Public License as published by
** the Free Software Foundation; either version 2 of the License, or
** (at your option) any later version.
**
** This program is distributed in the hope that it will be useful,
** but WITHOUT ANY WARRANTY; without even the implied warranty of
** MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
** GNU General Public License for more details.
**
** You should have received a copy of the GNU General Public License
** along with this program; if not, write to the Free Software
** Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
**
** $Id: hcb_9.c,v 1.1 2002/07/14 23:33:35 miguelfreitas Exp $
**/

#include "../common.h"
#include "hcb.h"

/* Binary search huffman table HCB_9 */


extern hcb_bin_pair hcb9[] = {
    { /*  0 */ 0, { 1, 2 } },
    { /*  1 */ 1, { 0, 0 } },
    { /*  2 */ 0, { 1, 2 } },
    { /*  3 */ 0, { 2, 3 } },
    { /*  4 */ 0, { 3, 4 } },
    { /*  5 */ 1, { 1,  0 } },
    { /*  6 */ 1, { 0,  1 } },
    { /*  7 */ 0, { 2, 3 } },
    { /*  8 */ 0, { 3, 4 } },
    { /*  9 */ 1, { 1,  1 } },
    { /* 10 */ 0, { 3, 4 } },
    { /* 11 */ 0, { 4, 5 } },
    { /* 12 */ 0, { 5, 6 } },
    { /* 13 */ 0, { 6, 7 } },
    { /* 14 */ 0, { 7, 8 } },
    { /* 15 */ 0, { 8, 9 } },
    { /* 16 */ 0, { 9, 10 } },
    { /* 17 */ 0, { 10, 11 } },
    { /* 18 */ 0, { 11, 12 } },
    { /* 19 */ 1, { 2,  1 } },
    { /* 20 */ 1, { 1,  2 } },
    { /* 21 */ 1, { 2,  0 } },
    { /* 22 */ 1, { 0,  2 } },
    { /* 23 */ 0, { 8, 9 } },
    { /* 24 */ 0, { 9, 10 } },
    { /* 25 */ 0, { 10, 11 } },
    { /* 26 */ 0, { 11, 12 } },
    { /* 27 */ 0, { 12, 13 } },
    { /* 28 */ 0, { 13, 14 } },
    { /* 29 */ 0, { 14, 15 } },
    { /* 30 */ 0, { 15, 16 } },
    { /* 31 */ 1, { 3,  1 } },
    { /* 32 */ 1, { 2,  2 } },
    { /* 33 */ 1, { 1,  3 } },
    { /* 34 */ 0, { 13, 14 } },
    { /* 35 */ 0, { 14, 15 } },
    { /* 36 */ 0, { 15, 16 } },
    { /* 37 */ 0, { 16, 17 } },
    { /* 38 */ 0, { 17, 18 } },
    { /* 39 */ 0, { 18, 19 } },
    { /* 40 */ 0, { 19, 20 } },
    { /* 41 */ 0, { 20, 21 } },
    { /* 42 */ 0, { 21, 22 } },
    { /* 43 */ 0, { 22, 23 } },
    { /* 44 */ 0, { 23, 24 } },
    { /* 45 */ 0, { 24, 25 } },
    { /* 46 */ 0, { 25, 26 } },
    { /* 47 */ 1, { 3,  0 } },
    { /* 48 */ 1, { 0,  3 } },
    { /* 49 */ 1, { 2,  3 } },
    { /* 50 */ 1, { 3,  2 } },
    { /* 51 */ 1, { 1,  4 } },
    { /* 52 */ 1, { 4,  1 } },
    { /* 53 */ 1, { 2,  4 } },
    { /* 54 */ 1, { 1,  5 } },
    { /* 55 */ 0, { 18, 19 } },
    { /* 56 */ 0, { 19, 20 } },
    { /* 57 */ 0, { 20, 21 } },
    { /* 58 */ 0, { 21, 22 } },
    { /* 59 */ 0, { 22, 23 } },
    { /* 60 */ 0, { 23, 24 } },
    { /* 61 */ 0, { 24, 25 } },
    { /* 62 */ 0, { 25, 26 } },
    { /* 63 */ 0, { 26, 27 } },
    { /* 64 */ 0, { 27, 28 } },
    { /* 65 */ 0, { 28, 29 } },
    { /* 66 */ 0, { 29, 30 } },
    { /* 67 */ 0, { 30, 31 } },
    { /* 68 */ 0, { 31, 32 } },
    { /* 69 */ 0, { 32, 33 } },
    { /* 70 */ 0, { 33, 34 } },
    { /* 71 */ 0, { 34, 35 } },
    { /* 72 */ 0, { 35, 36 } },
    { /* 73 */ 1, { 4,  2 } },
    { /* 74 */ 1, { 3,  3 } },
    { /* 75 */ 1, { 0,  4 } },
    { /* 76 */ 1, { 4,  0 } },
    { /* 77 */ 1, { 5,  1 } },
    { /* 78 */ 1, { 2,  5 } },
    { /* 79 */ 1, { 1,  6 } },
    { /* 80 */ 1, { 3,  4 } },
    { /* 81 */ 1, { 5,  2 } },
    { /* 82 */ 1, { 6,  1 } },
    { /* 83 */ 1, { 4,  3 } },
    { /* 84 */ 0, { 25, 26 } },
    { /* 85 */ 0, { 26, 27 } },
    { /* 86 */ 0, { 27, 28 } },
    { /* 87 */ 0, { 28, 29 } },
    { /* 88 */ 0, { 29, 30 } },
    { /* 89 */ 0, { 30, 31 } },
    { /* 90 */ 0, { 31, 32 } },
    { /* 91 */ 0, { 32, 33 } },
    { /* 92 */ 0, { 33, 34 } },
    { /* 93 */ 0, { 34, 35 } },
    { /* 94 */ 0, { 35, 36 } },
    { /* 95 */ 0, { 36, 37 } },
    { /* 96 */ 0, { 37, 38 } },
    { /* 97 */ 0, { 38, 39 } },
    { /* 98 */ 0, { 39, 40 } },
    { /* 99 */ 0, { 40, 41 } },
    { /* 00 */ 0, { 41, 42 } },
    { /* 01 */ 0, { 42, 43 } },
    { /* 02 */ 0, { 43, 44 } },
    { /* 03 */ 0, { 44, 45 } },
    { /* 04 */ 0, { 45, 46 } },
    { /* 05 */ 0, { 46, 47 } },
    { /* 06 */ 0, { 47, 48 } },
    { /* 07 */ 0, { 48, 49 } },
    { /* 08 */ 0, { 49, 50 } },
    { /* 09 */ 1, { 0,  5 } },
    { /* 10 */ 1, { 2,  6 } },
    { /* 11 */ 1, { 5,  0 } },
    { /* 12 */ 1, { 1,  7 } },
    { /* 13 */ 1, { 3,  5 } },
    { /* 14 */ 1, { 1,  8 } },
    { /* 15 */ 1, { 8,  1 } },
    { /* 16 */ 1, { 4,  4 } },
    { /* 17 */ 1, { 5,  3 } },
    { /* 18 */ 1, { 6,  2 } },
    { /* 19 */ 1, { 7,  1 } },
    { /* 20 */ 1, { 0,  6 } },
    { /* 21 */ 1, { 8,  2 } },
    { /* 22 */ 1, { 2,  8 } },
    { /* 23 */ 1, { 3,  6 } },
    { /* 24 */ 1, { 2,  7 } },
    { /* 25 */ 1, { 4,  5 } },
    { /* 26 */ 1, { 9,  1 } },
    { /* 27 */ 1, { 1,  9 } },
    { /* 28 */ 1, { 7,  2 } },
    { /* 29 */ 0, { 30, 31 } },
    { /* 30 */ 0, { 31, 32 } },
    { /* 31 */ 0, { 32, 33 } },
    { /* 32 */ 0, { 33, 34 } },
    { /* 33 */ 0, { 34, 35 } },
    { /* 34 */ 0, { 35, 36 } },
    { /* 35 */ 0, { 36, 37 } },
    { /* 36 */ 0, { 37, 38 } },
    { /* 37 */ 0, { 38, 39 } },
    { /* 38 */ 0, { 39, 40 } },
    { /* 39 */ 0, { 40, 41 } },
    { /* 40 */ 0, { 41, 42 } },
    { /* 41 */ 0, { 42, 43 } },
    { /* 42 */ 0, { 43, 44 } },
    { /* 43 */ 0, { 44, 45 } },
    { /* 44 */ 0, { 45, 46 } },
    { /* 45 */ 0, { 46, 47 } },
    { /* 46 */ 0, { 47, 48 } },
    { /* 47 */ 0, { 48, 49 } },
    { /* 48 */ 0, { 49, 50 } },
    { /* 49 */ 0, { 50, 51 } },
    { /* 50 */ 0, { 51, 52 } },
    { /* 51 */ 0, { 52, 53 } },
    { /* 52 */ 0, { 53, 54 } },
    { /* 53 */ 0, { 54, 55 } },
    { /* 54 */ 0, { 55, 56 } },
    { /* 55 */ 0, { 56, 57 } },
    { /* 56 */ 0, { 57, 58 } },
    { /* 57 */ 0, { 58, 59 } },
    { /* 58 */ 0, { 59, 60 } },
    { /* 59 */ 1, {  6,  0 } },
    { /* 60 */ 1, {  5,  4 } },
    { /* 61 */ 1, {  6,  3 } },
    { /* 62 */ 1, {  8,  3 } },
    { /* 63 */ 1, {  0,  7 } },
    { /* 64 */ 1, {  9,  2 } },
    { /* 65 */ 1, {  3,  8 } },
    { /* 66 */ 1, {  4,  6 } },
    { /* 67 */ 1, {  3,  7 } },
    { /* 68 */ 1, {  0,  8 } },
    { /* 69 */ 1, { 10,  1 } },
    { /* 70 */ 1, {  6,  4 } },
    { /* 71 */ 1, {  2,  9 } },
    { /* 72 */ 1, {  5,  5 } },
    { /* 73 */ 1, {  8,  0 } },
    { /* 74 */ 1, {  7,  0 } },
    { /* 75 */ 1, {  7,  3 } },
    { /* 76 */ 1, { 10,  2 } },
    { /* 77 */ 1, {  9,  3 } },
    { /* 78 */ 1, {  8,  4 } },
    { /* 79 */ 1, {  1, 10 } },
    { /* 80 */ 1, {  7,  4 } },
    { /* 81 */ 1, {  6,  5 } },
    { /* 82 */ 1, {  5,  6 } },
    { /* 83 */ 1, {  4,  8 } },
    { /* 84 */ 1, {  4,  7 } },
    { /* 85 */ 1, {  3,  9 } },
    { /* 86 */ 1, { 11,  1 } },
    { /* 87 */ 1, {  5,  8 } },
    { /* 88 */ 1, {  9,  0 } },
    { /* 89 */ 1, {  8,  5 } },
    { /* 90 */ 0, { 29, 30 } },
    { /* 91 */ 0, { 30, 31 } },
    { /* 92 */ 0, { 31, 32 } },
    { /* 93 */ 0, { 32, 33 } },
    { /* 94 */ 0, { 33, 34 } },
    { /* 95 */ 0, { 34, 35 } },
    { /* 96 */ 0, { 35, 36 } },
    { /* 97 */ 0, { 36, 37 } },
    { /* 98 */ 0, { 37, 38 } },
    { /* 99 */ 0, { 38, 39 } },
    { /* 00 */ 0, { 39, 40 } },
    { /* 01 */ 0, { 40, 41 } },
    { /* 02 */ 0, { 41, 42 } },
    { /* 03 */ 0, { 42, 43 } },
    { /* 04 */ 0, { 43, 44 } },
    { /* 05 */ 0, { 44, 45 } },
    { /* 06 */ 0, { 45, 46 } },
    { /* 07 */ 0, { 46, 47 } },
    { /* 08 */ 0, { 47, 48 } },
    { /* 09 */ 0, { 48, 49 } },
    { /* 10 */ 0, { 49, 50 } },
    { /* 11 */ 0, { 50, 51 } },
    { /* 12 */ 0, { 51, 52 } },
    { /* 13 */ 0, { 52, 53 } },
    { /* 14 */ 0, { 53, 54 } },
    { /* 15 */ 0, { 54, 55 } },
    { /* 16 */ 0, { 55, 56 } },
    { /* 17 */ 0, { 56, 57 } },
    { /* 18 */ 0, { 57, 58 } },
    { /* 19 */ 1, { 10,  3 } },
    { /* 20 */ 1, {  2, 10 } },
    { /* 21 */ 1, {  0,  9 } },
    { /* 22 */ 1, { 11,  2 } },
    { /* 23 */ 1, {  9,  4 } },
    { /* 24 */ 1, {  6,  6 } },
    { /* 25 */ 1, { 12,  1 } },
    { /* 26 */ 1, {  4,  9 } },
    { /* 27 */ 1, {  8,  6 } },
    { /* 28 */ 1, {  1, 11 } },
    { /* 29 */ 1, {  9,  5 } },
    { /* 30 */ 1, { 10,  4 } },
    { /* 31 */ 1, {  5,  7 } },
    { /* 32 */ 1, {  7,  5 } },
    { /* 33 */ 1, {  2, 11 } },
    { /* 34 */ 1, {  1, 12 } },
    { /* 35 */ 1, { 12,  2 } },
    { /* 36 */ 1, { 11,  3 } },
    { /* 37 */ 1, {  3, 10 } },
    { /* 38 */ 1, {  5,  9 } },
    { /* 39 */ 1, {  6,  7 } },
    { /* 40 */ 1, {  8,  7 } },
    { /* 41 */ 1, { 11,  4 } },
    { /* 42 */ 1, {  0, 10 } },
    { /* 43 */ 1, {  7,  6 } },
    { /* 44 */ 1, { 12,  3 } },
    { /* 45 */ 1, { 10,  0 } },
    { /* 46 */ 1, { 10,  5 } },
    { /* 47 */ 1, {  4, 10 } },
    { /* 48 */ 1, {  6,  8 } },
    { /* 49 */ 1, {  2, 12 } },
    { /* 50 */ 1, {  9,  6 } },
    { /* 51 */ 1, {  9,  7 } },
    { /* 52 */ 1, {  4, 11 } },
    { /* 53 */ 1, { 11,  0 } },
    { /* 54 */ 1, {  6,  9 } },
    { /* 55 */ 1, {  3, 11 } },
    { /* 56 */ 1, {  5, 10 } },
    { /* 57 */ 0, { 20, 21 } },
    { /* 58 */ 0, { 21, 22 } },
    { /* 59 */ 0, { 22, 23 } },
    { /* 60 */ 0, { 23, 24 } },
    { /* 61 */ 0, { 24, 25 } },
    { /* 62 */ 0, { 25, 26 } },
    { /* 63 */ 0, { 26, 27 } },
    { /* 64 */ 0, { 27, 28 } },
    { /* 65 */ 0, { 28, 29 } },
    { /* 66 */ 0, { 29, 30 } },
    { /* 67 */ 0, { 30, 31 } },
    { /* 68 */ 0, { 31, 32 } },
    { /* 69 */ 0, { 32, 33 } },
    { /* 70 */ 0, { 33, 34 } },
    { /* 71 */ 0, { 34, 35 } },
    { /* 72 */ 0, { 35, 36 } },
    { /* 73 */ 0, { 36, 37 } },
    { /* 74 */ 0, { 37, 38 } },
    { /* 75 */ 0, { 38, 39 } },
    { /* 76 */ 0, { 39, 40 } },
    { /* 77 */ 1, {  8,  8 } },
    { /* 78 */ 1, {  7,  8 } },
    { /* 79 */ 1, { 12,  5 } },
    { /* 80 */ 1, {  3, 12 } },
    { /* 81 */ 1, { 11,  5 } },
    { /* 82 */ 1, {  7,  7 } },
    { /* 83 */ 1, { 12,  4 } },
    { /* 84 */ 1, { 11,  6 } },
    { /* 85 */ 1, { 10,  6 } },
    { /* 86 */ 1, {  4, 12 } },
    { /* 87 */ 1, {  7,  9 } },
    { /* 88 */ 1, {  5, 11 } },
    { /* 89 */ 1, {  0, 11 } },
    { /* 90 */ 1, { 12,  6 } },
    { /* 91 */ 1, {  6, 10 } },
    { /* 92 */ 1, { 12,  0 } },
    { /* 93 */ 1, { 10,  7 } },
    { /* 94 */ 1, {  5, 12 } },
    { /* 95 */ 1, {  7, 10 } },
    { /* 96 */ 1, {  9,  8 } },
    { /* 97 */ 1, {  0, 12 } },
    { /* 98 */ 1, { 11,  7 } },
    { /* 99 */ 1, {  8,  9 } },
    { /* 00 */ 1, {  9,  9 } },
    { /* 01 */ 1, { 10,  8 } },
    { /* 02 */ 1, {  7, 11 } },
    { /* 03 */ 1, { 12,  7 } },
    { /* 04 */ 1, {  6, 11 } },
    { /* 05 */ 1, {  8, 11 } },
    { /* 06 */ 1, { 11,  8 } },
    { /* 07 */ 1, {  7, 12 } },
    { /* 08 */ 1, {  6, 12 } },
    { /* 09 */ 0, { 8, 9 } },
    { /* 10 */ 0, { 9, 10 } },
    { /* 11 */ 0, { 10, 11 } },
    { /* 12 */ 0, { 11, 12 } },
    { /* 13 */ 0, { 12, 13 } },
    { /* 14 */ 0, { 13, 14 } },
    { /* 15 */ 0, { 14, 15 } },
    { /* 16 */ 0, { 15, 16 } },
    { /* 17 */ 1, {  8, 10 } },
    { /* 18 */ 1, { 10,  9 } },
    { /* 19 */ 1, {  8, 12 } },
    { /* 20 */ 1, {  9, 10 } },
    { /* 21 */ 1, {  9, 11 } },
    { /* 22 */ 1, {  9, 12 } },
    { /* 23 */ 1, { 10, 11 } },
    { /* 24 */ 1, { 12,  9 } },
    { /* 25 */ 1, { 10, 10 } },
    { /* 26 */ 1, { 11,  9 } },
    { /* 27 */ 1, { 12,  8 } },
    { /* 28 */ 1, { 11, 10 } },
    { /* 29 */ 1, { 12, 10 } },
    { /* 30 */ 1, { 12, 11 } },
    { /* 31 */ 0, { 2, 3 } },
    { /* 32 */ 0, { 3, 4 } },
    { /* 33 */ 1, { 10, 12 } },
    { /* 34 */ 1, { 11, 11 } },
    { /* 35 */ 1, { 11, 12 } },
    { /* 36 */ 1, { 12, 12 } }
};
