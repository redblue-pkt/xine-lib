/*  Common SCSI Multimedia Command (MMC) routines.

    $Id: scsi_mmc.c,v 1.1 2005/01/01 02:43:57 rockyb Exp $

    Copyright (C) 2004 Rocky Bernstein <rocky@panix.com>

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

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include <cdio/cdio.h>
#include <cdio/logging.h>
#include <cdio/scsi_mmc.h>
#include "cdio_private.h"

#ifdef HAVE_STRING_H
#include <string.h>
#endif

#ifdef HAVE_STDLIB_H
#include <stdlib.h>
#endif

#ifdef HAVE_ERRNO_H
#include <errno.h>
#endif

/*!
  On input a MODE_SENSE command was issued and we have the results
  in p. We interpret this and return a bit mask set according to the 
  capabilities.
 */
void
scsi_mmc_get_drive_cap_buf(const uint8_t *p,
			   /*out*/ cdio_drive_read_cap_t  *p_read_cap,
			   /*out*/ cdio_drive_write_cap_t *p_write_cap,
			   /*out*/ cdio_drive_misc_cap_t  *p_misc_cap)
{
  /* Reader */
  if (p[2] & 0x01) *p_read_cap  |= CDIO_DRIVE_CAP_READ_CD_R;
  if (p[2] & 0x02) *p_read_cap  |= CDIO_DRIVE_CAP_READ_CD_RW;
  if (p[2] & 0x08) *p_read_cap  |= CDIO_DRIVE_CAP_READ_DVD_ROM;
  if (p[4] & 0x01) *p_read_cap  |= CDIO_DRIVE_CAP_READ_AUDIO;
  if (p[5] & 0x01) *p_read_cap  |= CDIO_DRIVE_CAP_READ_CD_DA;
  if (p[5] & 0x10) *p_read_cap  |= CDIO_DRIVE_CAP_READ_C2_ERRS;
  
  /* Writer */
  if (p[3] & 0x01) *p_write_cap |= CDIO_DRIVE_CAP_WRITE_CD_R;
  if (p[3] & 0x02) *p_write_cap |= CDIO_DRIVE_CAP_WRITE_CD_RW;
  if (p[3] & 0x10) *p_write_cap |= CDIO_DRIVE_CAP_WRITE_DVD_R;
  if (p[3] & 0x20) *p_write_cap |= CDIO_DRIVE_CAP_WRITE_DVD_RAM;
  if (p[4] & 0x80) *p_misc_cap  |= CDIO_DRIVE_CAP_WRITE_BURN_PROOF;

  /* Misc */
  if (p[4] & 0x40) *p_misc_cap  |= CDIO_DRIVE_CAP_MISC_MULTI_SESSION;
  if (p[6] & 0x01) *p_misc_cap  |= CDIO_DRIVE_CAP_MISC_LOCK;
  if (p[6] & 0x08) *p_misc_cap  |= CDIO_DRIVE_CAP_MISC_EJECT;
  if (p[6] >> 5 != 0) 
    *p_misc_cap |= CDIO_DRIVE_CAP_MISC_CLOSE_TRAY;
}

/*!  
  Return the number of length in bytes of the Command Descriptor
  buffer (CDB) for a given SCSI MMC command. The length will be 
  either 6, 10, or 12. 
*/
uint8_t
scsi_mmc_get_cmd_len(uint8_t scsi_cmd) 
{
  static const uint8_t scsi_cdblen[8] = {6, 10, 10, 12, 12, 12, 10, 10};
  return scsi_cdblen[((scsi_cmd >> 5) & 7)];
}

/*!
  Run a SCSI MMC command. 
 
  cdio	        CD structure set by cdio_open().
  i_timeout     time in milliseconds we will wait for the command
                to complete. If this value is -1, use the default 
		time-out value.
  buf	        Buffer for data, both sending and receiving
  len	        Size of buffer
  e_direction	direction the transfer is to go
  cdb	        CDB bytes. All values that are needed should be set on 
                input. We'll figure out what the right CDB length should be.

  We return 0 if command completed successfully and 1 if not.
 */
int
scsi_mmc_run_cmd( const CdIo *p_cdio, unsigned int i_timeout_ms, 
		  const scsi_mmc_cdb_t *p_cdb,
		  scsi_mmc_direction_t e_direction, unsigned int i_buf, 
		  /*in/out*/ void *p_buf )
{
  if (p_cdio && p_cdio->op.run_scsi_mmc_cmd) {
    return p_cdio->op.run_scsi_mmc_cmd(p_cdio->env, i_timeout_ms,
				       scsi_mmc_get_cmd_len(p_cdb->field[0]),
				       p_cdb, e_direction, i_buf, p_buf);
  } else 
    return 1;
}

#define DEFAULT_TIMEOUT_MS 6000
  
/*!
 * Eject using SCSI MMC commands. Return 0 if successful.
 */
int 
scsi_mmc_eject_media( const CdIo *cdio )
{
  int i_status;
  scsi_mmc_cdb_t cdb = {{0, }};
  uint8_t buf[1];
  scsi_mmc_run_cmd_fn_t run_scsi_mmc_cmd;

  if ( ! cdio || ! cdio->op.run_scsi_mmc_cmd )
    return -2;

  run_scsi_mmc_cmd = cdio->op.run_scsi_mmc_cmd;
  
  CDIO_MMC_SET_COMMAND(cdb.field, CDIO_MMC_GPCMD_ALLOW_MEDIUM_REMOVAL);

  i_status = run_scsi_mmc_cmd (cdio->env, DEFAULT_TIMEOUT_MS,
			       scsi_mmc_get_cmd_len(cdb.field[0]), &cdb, 
			       SCSI_MMC_DATA_WRITE, 0, &buf);
  if (0 != i_status)
    return i_status;
  
  CDIO_MMC_SET_COMMAND(cdb.field, CDIO_MMC_GPCMD_START_STOP);
  cdb.field[4] = 1;
  i_status = run_scsi_mmc_cmd (cdio->env, DEFAULT_TIMEOUT_MS,
			       scsi_mmc_get_cmd_len(cdb.field[0]), &cdb, 
			       SCSI_MMC_DATA_WRITE, 0, &buf);
  if (0 != i_status)
    return i_status;
  
  CDIO_MMC_SET_COMMAND(cdb.field, CDIO_MMC_GPCMD_START_STOP);
  cdb.field[4] = 2; /* eject */

  return run_scsi_mmc_cmd (cdio->env, DEFAULT_TIMEOUT_MS,
			   scsi_mmc_get_cmd_len(cdb.field[0]), &cdb, 
			   SCSI_MMC_DATA_WRITE, 0, &buf);
  
}

/*! Packet driver to read mode2 sectors. 
   Can read only up to 25 blocks.
*/
int
scsi_mmc_read_sectors ( const CdIo *cdio, void *p_buf, lba_t lba, 
			int sector_type, unsigned int nblocks )
{
  scsi_mmc_cdb_t cdb = {{0, }};

  scsi_mmc_run_cmd_fn_t run_scsi_mmc_cmd;

  if ( ! cdio || ! cdio->op.run_scsi_mmc_cmd )
    return -2;

  run_scsi_mmc_cmd = cdio->op.run_scsi_mmc_cmd;

  CDIO_MMC_SET_COMMAND(cdb.field, CDIO_MMC_GPCMD_READ_CD);
  CDIO_MMC_SET_READ_TYPE    (cdb.field, sector_type);
  CDIO_MMC_SET_READ_LBA     (cdb.field, lba);
  CDIO_MMC_SET_READ_LENGTH24(cdb.field, nblocks);
  CDIO_MMC_SET_MAIN_CHANNEL_SELECTION_BITS(cdb.field, 
					   CDIO_MMC_MCSB_ALL_HEADERS);

  return run_scsi_mmc_cmd (cdio->env, DEFAULT_TIMEOUT_MS,
			   scsi_mmc_get_cmd_len(cdb.field[0]), &cdb, 
			   SCSI_MMC_DATA_READ, 
			   CDIO_CD_FRAMESIZE_RAW * nblocks,
			   p_buf);
}

int
scsi_mmc_set_blocksize_private ( const void *p_env, 
				 const scsi_mmc_run_cmd_fn_t run_scsi_mmc_cmd, 
				 unsigned int bsize)
{
  scsi_mmc_cdb_t cdb = {{0, }};

  struct
  {
    uint8_t reserved1;
    uint8_t medium;
    uint8_t reserved2;
    uint8_t block_desc_length;
    uint8_t density;
    uint8_t number_of_blocks_hi;
    uint8_t number_of_blocks_med;
    uint8_t number_of_blocks_lo;
    uint8_t reserved3;
    uint8_t block_length_hi;
    uint8_t block_length_med;
    uint8_t block_length_lo;
  } mh;

  if ( ! p_env || ! run_scsi_mmc_cmd )
    return -2;

  memset (&mh, 0, sizeof (mh));
  mh.block_desc_length = 0x08;
  mh.block_length_hi   = (bsize >> 16) & 0xff;
  mh.block_length_med  = (bsize >>  8) & 0xff;
  mh.block_length_lo   = (bsize >>  0) & 0xff;

  CDIO_MMC_SET_COMMAND(cdb.field, CDIO_MMC_GPCMD_MODE_SELECT_6);

  cdb.field[1] = 1 << 4;
  cdb.field[4] = 12;
  
  return run_scsi_mmc_cmd (p_env, DEFAULT_TIMEOUT_MS,
			      scsi_mmc_get_cmd_len(cdb.field[0]), &cdb, 
			      SCSI_MMC_DATA_WRITE, sizeof(mh), &mh);
}

int 
scsi_mmc_set_blocksize ( const CdIo *cdio, unsigned int bsize)
{
  if ( ! cdio )  return -2;
  return 
    scsi_mmc_set_blocksize_private (cdio->env, cdio->op.run_scsi_mmc_cmd, 
				    bsize);
}


/*!
  Return the the kind of drive capabilities of device.
 */
void
scsi_mmc_get_drive_cap_private (const void *p_env,
				const scsi_mmc_run_cmd_fn_t run_scsi_mmc_cmd, 
				/*out*/ cdio_drive_read_cap_t  *p_read_cap,
				/*out*/ cdio_drive_write_cap_t *p_write_cap,
				/*out*/ cdio_drive_misc_cap_t  *p_misc_cap)
{
  /* Largest buffer size we use. */
#define BUF_MAX 2048
  uint8_t buf[BUF_MAX] = { 0, };

  scsi_mmc_cdb_t cdb = {{0, }};
  int i_status;
  uint16_t i_data = BUF_MAX;
  
  if ( ! p_env || ! run_scsi_mmc_cmd )
    return;

  CDIO_MMC_SET_COMMAND(cdb.field, CDIO_MMC_GPCMD_MODE_SENSE_10);
  cdb.field[1] = 0x0;  
  cdb.field[2] = CDIO_MMC_ALL_PAGES; 

 retry:
  CDIO_MMC_SET_READ_LENGTH16(cdb.field, 8);

  /* In the first run we run MODE SENSE 10 we are trying to get the
     length of the data features. */
  i_status = run_scsi_mmc_cmd (p_env, DEFAULT_TIMEOUT_MS,
			       scsi_mmc_get_cmd_len(cdb.field[0]), 
			       &cdb, SCSI_MMC_DATA_READ, 
			       sizeof(buf), &buf);
  if (0 == i_status) {
    uint16_t i_data_try = (uint16_t) CDIO_MMC_GET_LEN16(buf);
    if (i_data_try < BUF_MAX) i_data = i_data_try;
  }

  /* Now try getting all features with length set above, possibly
     truncated or the default length if we couldn't get the proper
     length. */
  CDIO_MMC_SET_READ_LENGTH16(cdb.field, i_data);

  i_status = run_scsi_mmc_cmd (p_env, DEFAULT_TIMEOUT_MS,
			       scsi_mmc_get_cmd_len(cdb.field[0]), 
			       &cdb, SCSI_MMC_DATA_READ, 
			       sizeof(buf), &buf);

  if (0 != i_status && CDIO_MMC_CAPABILITIES_PAGE != cdb.field[2]) {
    cdb.field[2] =  CDIO_MMC_CAPABILITIES_PAGE; 
    goto retry;
  }

  if (0 == i_status) {
    uint8_t *p;
    uint8_t *p_max = buf + 256;
    
    *p_read_cap  = 0;
    *p_write_cap = 0;
    *p_misc_cap  = 0;

    /* set to first sense mask, and then walk through the masks */
    p = buf + 8;
    while( (p < &(buf[2+i_data])) && (p < p_max) )       {
      uint8_t which_page;
      
      which_page = p[0] & 0x3F;
      switch( which_page )
	{
	case CDIO_MMC_AUDIO_CTL_PAGE:
	case CDIO_MMC_R_W_ERROR_PAGE:
	case CDIO_MMC_CDR_PARMS_PAGE:
	  /* Don't handle these yet. */
	  break;
	case CDIO_MMC_CAPABILITIES_PAGE:
	  scsi_mmc_get_drive_cap_buf(p, p_read_cap, p_write_cap, p_misc_cap);
	  break;
	default: ;
	}
      p += (p[1] + 2);
    }
  } else {
    cdio_info("%s: %s\n", "error in MODE_SELECT", strerror(errno));
    *p_read_cap  = CDIO_DRIVE_CAP_ERROR;
    *p_write_cap = CDIO_DRIVE_CAP_ERROR;
    *p_misc_cap  = CDIO_DRIVE_CAP_ERROR;
  }
  return;
}

void
scsi_mmc_get_drive_cap (const CdIo *p_cdio,
			/*out*/ cdio_drive_read_cap_t  *p_read_cap,
			/*out*/ cdio_drive_write_cap_t *p_write_cap,
			/*out*/ cdio_drive_misc_cap_t  *p_misc_cap)
{
  if ( ! p_cdio )  return;
  scsi_mmc_get_drive_cap_private (p_cdio->env, 
				  p_cdio->op.run_scsi_mmc_cmd, 
				  p_read_cap, p_write_cap, p_misc_cap);
}

void
scsi_mmc_get_drive_cap_generic (const void *p_user_data,
				/*out*/ cdio_drive_read_cap_t  *p_read_cap,
				/*out*/ cdio_drive_write_cap_t *p_write_cap,
				/*out*/ cdio_drive_misc_cap_t  *p_misc_cap)
{
  const generic_img_private_t *p_env = p_user_data;
  scsi_mmc_get_drive_cap( p_env->cdio,
			  p_read_cap, p_write_cap, p_misc_cap );
}


/*! 
  Get the DVD type associated with cd object.
*/
discmode_t
scsi_mmc_get_dvd_struct_physical_private ( void *p_env, const 
					   scsi_mmc_run_cmd_fn_t run_scsi_mmc_cmd, 
					   cdio_dvd_struct_t *s)
{
  scsi_mmc_cdb_t cdb = {{0, }};
  unsigned char buf[4 + 4 * 20], *base;
  int i_status;
  uint8_t layer_num = s->physical.layer_num;
  
  cdio_dvd_layer_t *layer;
  
  if ( ! p_env || ! run_scsi_mmc_cmd )
    return -2;

  if (layer_num >= CDIO_DVD_MAX_LAYERS)
    return -EINVAL;
  
  CDIO_MMC_SET_COMMAND(cdb.field, CDIO_MMC_GPCMD_READ_DVD_STRUCTURE);
  cdb.field[6] = layer_num;
  cdb.field[7] = CDIO_DVD_STRUCT_PHYSICAL;
  cdb.field[9] = sizeof(buf) & 0xff;
  
  i_status = run_scsi_mmc_cmd(p_env, DEFAULT_TIMEOUT_MS, 
			      scsi_mmc_get_cmd_len(cdb.field[0]), 
			      &cdb, SCSI_MMC_DATA_READ, 
			      sizeof(buf), &buf);
  if (0 != i_status)
    return CDIO_DISC_MODE_ERROR;
  
  base = &buf[4];
  layer = &s->physical.layer[layer_num];
  
  /*
   * place the data... really ugly, but at least we won't have to
   * worry about endianess in userspace.
   */
  memset(layer, 0, sizeof(*layer));
  layer->book_version = base[0] & 0xf;
  layer->book_type = base[0] >> 4;
  layer->min_rate = base[1] & 0xf;
  layer->disc_size = base[1] >> 4;
  layer->layer_type = base[2] & 0xf;
  layer->track_path = (base[2] >> 4) & 1;
  layer->nlayers = (base[2] >> 5) & 3;
  layer->track_density = base[3] & 0xf;
  layer->linear_density = base[3] >> 4;
  layer->start_sector = base[5] << 16 | base[6] << 8 | base[7];
  layer->end_sector = base[9] << 16 | base[10] << 8 | base[11];
  layer->end_sector_l0 = base[13] << 16 | base[14] << 8 | base[15];
  layer->bca = base[16] >> 7;

  return 0;
}


/*! 
  Get the DVD type associated with cd object.
*/
discmode_t
scsi_mmc_get_dvd_struct_physical ( const CdIo *p_cdio, cdio_dvd_struct_t *s)
{
  if ( ! p_cdio )  return -2;
  return 
    scsi_mmc_get_dvd_struct_physical_private (p_cdio->env, 
					      p_cdio->op.run_scsi_mmc_cmd, 
					      s);
}

/*! 
  Get the CD-ROM hardware info via a SCSI MMC INQUIRY command.
  False is returned if we had an error getting the information.
*/
bool 
scsi_mmc_get_hwinfo ( const CdIo *p_cdio, 
		      /*out*/ cdio_hwinfo_t *hw_info )
{
  int i_status;                  /* Result of SCSI MMC command */
  char buf[36] = { 0, };         /* Place to hold returned data */
  scsi_mmc_cdb_t cdb = {{0, }};  /* Command Descriptor Block */
  
  CDIO_MMC_SET_COMMAND(cdb.field, CDIO_MMC_GPCMD_INQUIRY);
  cdb.field[4] = sizeof(buf);

  if (! p_cdio || ! hw_info ) return false;
  
  i_status = scsi_mmc_run_cmd(p_cdio, DEFAULT_TIMEOUT_MS, 
			      &cdb, SCSI_MMC_DATA_READ, 
			      sizeof(buf), &buf);
  if (i_status == 0) {
      
      memcpy(hw_info->psz_vendor, 
	     buf + 8, 
	     sizeof(hw_info->psz_vendor)-1);
      hw_info->psz_vendor[sizeof(hw_info->psz_vendor)-1] = '\0';
      memcpy(hw_info->psz_model,  
	     buf + 8 + CDIO_MMC_HW_VENDOR_LEN, 
	     sizeof(hw_info->psz_model)-1);
      hw_info->psz_model[sizeof(hw_info->psz_model)-1] = '\0';
      memcpy(hw_info->psz_revision, 
	     buf + 8 + CDIO_MMC_HW_VENDOR_LEN + CDIO_MMC_HW_MODEL_LEN,
	     sizeof(hw_info->psz_revision)-1);
      hw_info->psz_revision[sizeof(hw_info->psz_revision)-1] = '\0';
      return true;
    }
  return false;
}

/*!
  Return the media catalog number MCN.

  Note: string is malloc'd so caller should free() then returned
  string when done with it.

 */
char *
scsi_mmc_get_mcn_private ( void *p_env,
			   const scsi_mmc_run_cmd_fn_t run_scsi_mmc_cmd
			   )
{
  scsi_mmc_cdb_t cdb = {{0, }};
  char buf[28] = { 0, };
  int i_status;

  if ( ! p_env || ! run_scsi_mmc_cmd )
    return NULL;

  CDIO_MMC_SET_COMMAND(cdb.field, CDIO_MMC_GPCMD_READ_SUBCHANNEL);
  cdb.field[1] = 0x0;  
  cdb.field[2] = 0x40; 
  cdb.field[3] = CDIO_SUBCHANNEL_MEDIA_CATALOG;
  CDIO_MMC_SET_READ_LENGTH16(cdb.field, sizeof(buf));

  i_status = run_scsi_mmc_cmd(p_env, DEFAULT_TIMEOUT_MS, 
			      scsi_mmc_get_cmd_len(cdb.field[0]), 
			      &cdb, SCSI_MMC_DATA_READ, 
			      sizeof(buf), buf);
  if(i_status == 0) {
    return strdup(&buf[9]);
  }
  return NULL;
}

char *
scsi_mmc_get_mcn ( const CdIo *p_cdio )
{
  if ( ! p_cdio )  return NULL;
  return scsi_mmc_get_mcn_private (p_cdio->env, 
				   p_cdio->op.run_scsi_mmc_cmd );
}

char *
scsi_mmc_get_mcn_generic (const void *p_user_data)
{
  const generic_img_private_t *p_env = p_user_data;
  return scsi_mmc_get_mcn( p_env->cdio );
}

/*
  Read cdtext information for a CdIo object .
  
  return true on success, false on error or CD-Text information does
  not exist.
*/
bool
scsi_mmc_init_cdtext_private ( void *p_user_data, 
			       const scsi_mmc_run_cmd_fn_t run_scsi_mmc_cmd,
			       set_cdtext_field_fn_t set_cdtext_field_fn 
			       )
{

  generic_img_private_t *p_env = p_user_data;
  scsi_mmc_cdb_t  cdb = {{0, }};
  unsigned char   wdata[5000] = { 0, };
  int             i_status, i_errno;

  if ( ! p_env || ! run_scsi_mmc_cmd || p_env->b_cdtext_error )
    return false;

  /* Operation code */
  CDIO_MMC_SET_COMMAND(cdb.field, CDIO_MMC_GPCMD_READ_TOC);

  cdb.field[1] = CDIO_CDROM_MSF;
  /* Format */
  cdb.field[2] = CDIO_MMC_READTOC_FMT_CDTEXT;

  /* Setup to read header, to get length of data */
  CDIO_MMC_SET_READ_LENGTH16(cdb.field, 4);

  errno = 0;

/* Set read timeout 3 minues. */
#define READ_TIMEOUT 3*60*1000

  /* We may need to give CD-Text a little more time to complete. */
  /* First off, just try and read the size */
  i_status = run_scsi_mmc_cmd (p_env, READ_TIMEOUT,
			       scsi_mmc_get_cmd_len(cdb.field[0]), 
			       &cdb, SCSI_MMC_DATA_READ, 
			       4, &wdata);

  if (i_status != 0) {
    cdio_info ("CD-Text read failed for header: %s\n", strerror(errno));  
	i_errno = errno;
    p_env->b_cdtext_error = true;
    return false;
  } else {
    /* Now read the CD-Text data */
    int	i_cdtext = CDIO_MMC_GET_LEN16(wdata);

    if (i_cdtext > sizeof(wdata)) i_cdtext = sizeof(wdata);
    
    CDIO_MMC_SET_READ_LENGTH16(cdb.field, i_cdtext);
    i_status = run_scsi_mmc_cmd (p_env, READ_TIMEOUT,
				 scsi_mmc_get_cmd_len(cdb.field[0]), 
				 &cdb, SCSI_MMC_DATA_READ, 
				 i_cdtext, &wdata);
    if (i_status != 0) {
      cdio_info ("CD-Text read for text failed: %s\n", strerror(errno));  
      i_errno = errno;
      p_env->b_cdtext_error = true;
      return false;
    }
    p_env->b_cdtext_init = true;
    return cdtext_data_init(p_env, p_env->i_first_track, wdata, 
			    set_cdtext_field_fn);
  }
}

