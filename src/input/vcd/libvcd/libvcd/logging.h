/*
    $Id: logging.h,v 1.1 2003/10/13 11:47:12 f1rmb Exp $

    Copyright (C) 2000 Herbert Valerio Riedel <hvr@gnu.org>

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
*/

#ifndef __VCD_LOGGING_H__
#define __VCD_LOGGING_H__

#include <libvcd/types.h>

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

typedef enum {
  VCD_LOG_DEBUG = 1,
  VCD_LOG_INFO,
  VCD_LOG_WARN,
  VCD_LOG_ERROR,
  VCD_LOG_ASSERT
} vcd_log_level_t;

void
vcd_log (vcd_log_level_t level, const char format[], ...) GNUC_PRINTF(2, 3);
    
typedef void (*vcd_log_handler_t) (vcd_log_level_t level, 
                                   const char message[]);

vcd_log_handler_t
vcd_log_set_handler (vcd_log_handler_t new_handler);

void
vcd_debug (const char format[], ...) GNUC_PRINTF(1,2);

void
vcd_info (const char format[], ...) GNUC_PRINTF(1,2);

void
vcd_warn (const char format[], ...) GNUC_PRINTF(1,2);

void
vcd_error (const char format[], ...) GNUC_PRINTF(1,2);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* __VCD_LOGGING_H__ */


/* 
 * Local variables:
 *  c-file-style: "gnu"
 *  tab-width: 8
 *  indent-tabs-mode: nil
 * End:
 */
