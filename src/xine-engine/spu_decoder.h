/* 
 * Copyright (C) 2000-2001 the xine project
 * 
 * Copyright (C) James Courtier-Dutton James@superbug.demon.co.uk - July 2001
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
 * $Id: spu_decoder.h,v 1.4 2001/07/18 21:38:17 f1rmb Exp $
 */
#ifndef HAVE_SPU_OUT_H
#define HAVE_SPU_OUT_H

#ifdef __cplusplus
extern "C" {
#endif

#include <inttypes.h>

#if defined(XINE_COMPILE)
#include "metronom.h"
#include "configfile.h"
#endif


#define SPU_OUT_IFACE_VERSION  1

/*
 * spu_functions_s contains the functions every spu output
 * driver plugin has to implement.
 */

typedef struct spu_functions_s spu_functions_t;

struct spu_functions_s {

  /* 
   *
   * find out what output modes + capatilities are supported by 
   * this plugin (constants for the bit vector to return see above)
   *
   * See SPU_CAP_* bellow.
   */
  uint32_t (*get_capabilities) (spu_functions_t *this);

  /*
   * connect this driver to the xine engine
   */
  void (*connect) (spu_functions_t *this, metronom_t *metronom);

  /*
   * open the driver and make it ready to receive spu data 
   * buffers may be flushed(!)
   *
   * return value: <=0 : failure, 1 : ok
   */

  int (*open)(spu_functions_t *this, uint32_t bits, uint32_t rate, int mode);

  /*
   * write spu data to output buffer - may block
   * spu driver must sync sample playback with metronom
   */

  void (*write_spu_data)(spu_functions_t *this,
			   int16_t* spu_data, uint32_t num_samples, 
			   uint32_t pts);

  /*
   * this is called when the decoder no longer uses the spu
   * output driver - the driver should get ready to get opened() again
   */

  void (*close)(spu_functions_t *this);

  /*
   * shut down this spu output driver plugin and
   * free all resources allocated
   */

  void (*exit) (spu_functions_t *this);

  /*
   * Get, Set a property of spu driver.
   *
   * get_property() return 1 in success, 0 on failure.
   * set_property() return value on success, ~value on failure.
   *
   * See AC_PROP_* bellow for available properties.
   */
  int (*get_property) (spu_functions_t *this, int property);

  int (*set_property) (spu_functions_t *this,  int property, int value);

};


/*
 * to build a dynamic spu output plugin,
 * you have to implement these functions:
 *
 *
 * spu_functions_t *init_spu_out_plugin (config_values_t *config)
 *
 * init this plugin, check if device is available
 *
 * spu_info_t *get_spu_out_plugin_info ()
 *
 * peek at some (static) information about the plugin without initializing it
 *
 */

/*
 * spu output modes + capabilities
 */

/* none yet */

typedef struct spu_info_s {

  int     interface_version;
  char   *id;
  char   *description;
  int     priority;
} spu_info_t ;

#ifdef __cplusplus
}
#endif

#endif
