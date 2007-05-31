/*
    $Id: cdio_private.h,v 1.3 2005/01/01 02:43:57 rockyb Exp $

    Copyright (C) 2003, 2004 Rocky Bernstein <rocky@panix.com>

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

/* Internal routines for CD I/O drivers. */


#ifndef __CDIO_PRIVATE_H__
#define __CDIO_PRIVATE_H__

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include <cdio/cdio.h>
#include <cdio/cdtext.h>
#include "scsi_mmc_private.h"

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

  /* Opaque type */
  typedef struct _CdioDataSource CdioDataSource;

#ifdef __cplusplus
}

#endif /* __cplusplus */

#include "generic.h"

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */


  typedef struct {
    
    /*!
      Eject media in CD drive. If successful, as a side effect we 
      also free obj. Return 0 if success and 1 for failure.
    */
    int (*eject_media) (void *env);
    
    /*!
      Release and free resources associated with cd. 
    */
    void (*free) (void *env);
    
    /*!
      Return the value associated with the key "arg".
    */
    const char * (*get_arg) (void *env, const char key[]);
    
    /*! 
      Get cdtext information for a CdIo object.
    
      @param obj the CD object that may contain CD-TEXT information.
      @return the CD-TEXT object or NULL if obj is NULL
      or CD-TEXT information does not exist.
    
      If i_track is 0 or CDIO_CDROM_LEADOUT_TRACK the track returned
      is the information assocated with the CD. 
    */
    const cdtext_t * (*get_cdtext) (void *env, track_t i_track);
    
    /*!
      Return an array of device names. if CdIo is NULL (we haven't
      initialized a specific device driver), then find a suitable device 
      driver.
      
      NULL is returned if we couldn't return a list of devices.
    */
    char ** (*get_devices) (void);
    
    /*!
      Return a string containing the default CD device if none is specified.
    */
    char * (*get_default_device)(void);
    
    /*! 
      Get disc mode associated with cd_obj.
    */
    discmode_t (*get_discmode) (void *p_env);

    /*!
      Return the what kind of device we've got.
      
      See cd_types.h for a list of bitmasks for the drive type;
    */
    void (*get_drive_cap) (const void *env,
			   cdio_drive_read_cap_t  *p_read_cap,
			   cdio_drive_write_cap_t *p_write_cap,
			   cdio_drive_misc_cap_t  *p_misc_cap);
    /*!
      Return the number of of the first track. 
      CDIO_INVALID_TRACK is returned on error.
    */
    track_t (*get_first_track_num) (void *p_env);
    
    /*! 
      Get the CD-ROM hardware info via a SCSI MMC INQUIRY command.
      False is returned if we had an error getting the information.
    */
    bool (*get_hwinfo) ( const CdIo *p_cdio, 
			 /* out*/ cdio_hwinfo_t *p_hw_info );

    /*!  
      Return the media catalog number MCN from the CD or NULL if
      there is none or we don't have the ability to get it.
    */
    char * (*get_mcn) (const void *env);

    /*! 
      Return the number of tracks in the current medium.
      CDIO_INVALID_TRACK is returned on error.
    */
    track_t (*get_num_tracks) (void *env);
    
    /*!  
      Return the starting LBA for track number
      track_num in obj.  Tracks numbers start at 1.
      The "leadout" track is specified either by
      using track_num LEADOUT_TRACK or the total tracks+1.
      CDIO_INVALID_LBA is returned on error.
    */
    lba_t (*get_track_lba) (void *env, track_t track_num);
    
    /*!  
      Get format of track. 
    */
    track_format_t (*get_track_format) (void *env, track_t track_num);
    
    /*!
      Return true if we have XA data (green, mode2 form1) or
      XA data (green, mode2 form2). That is track begins:
      sync - header - subheader
      12     4      -  8
      
      FIXME: there's gotta be a better design for this and get_track_format?
    */
    bool (*get_track_green) (void *env, track_t track_num);
    
    /*!  
      Return the starting MSF (minutes/secs/frames) for track number
      track_num in obj.  Tracks numbers start at 1.
      The "leadout" track is specified either by
      using track_num LEADOUT_TRACK or the total tracks+1.
      False is returned on error.
    */
    bool (*get_track_msf) (void *env, track_t track_num, msf_t *msf);
    
    /*!
      lseek - reposition read/write file offset
      Returns (off_t) -1 on error. 
      Similar to libc's lseek()
    */
    off_t (*lseek) (void *env, off_t offset, int whence);
    
    /*!
      Reads into buf the next size bytes.
      Returns -1 on error. 
      Similar to libc's read()
    */
    ssize_t (*read) (void *env, void *buf, size_t size);
    
    /*!
      Reads a single mode2 sector from cd device into buf starting
      from lsn. Returns 0 if no error. 
    */
    int (*read_audio_sectors) (void *env, void *buf, lsn_t lsn,
			       unsigned int nblocks);
    
    /*!
      Reads a single mode2 sector from cd device into buf starting
      from lsn. Returns 0 if no error. 
    */
    int (*read_mode2_sector) (void *env, void *buf, lsn_t lsn, 
			      bool mode2_form2);
    
    /*!
      Reads nblocks of mode2 sectors from cd device into data starting
      from lsn.
      Returns 0 if no error. 
    */
    int (*read_mode2_sectors) (void *p_env, void *p_buf, lsn_t lsn, 
			       bool mode2_form2, unsigned int nblocks);
    
    /*!
      Reads a single mode1 sector from cd device into buf starting
      from lsn. Returns 0 if no error. 
    */
    int (*read_mode1_sector) (void *p_env, void *p_buf, lsn_t lsn, 
			      bool mode1_form2);
    
    /*!
      Reads nblocks of mode1 sectors from cd device into data starting
      from lsn.
      Returns 0 if no error. 
    */
    int (*read_mode1_sectors) (void *p_env, void *p_buf, lsn_t lsn, 
			       bool mode1_form2, unsigned int nblocks);
    
    bool (*read_toc) ( void *p_env ) ;

    /*!
      Run a SCSI MMC command. 
      
      cdio	        CD structure set by cdio_open().
      i_timeout_ms      time in milliseconds we will wait for the command
                        to complete. 
      cdb_len           number of bytes in cdb (6, 10, or 12).
      cdb	        CDB bytes. All values that are needed should be set on 
                        input. 
      b_return_data	TRUE if the command expects data to be returned in 
                        the buffer
      len	        Size of buffer
      buf	        Buffer for data, both sending and receiving
      
      Returns 0 if command completed successfully.
    */
    scsi_mmc_run_cmd_fn_t run_scsi_mmc_cmd;

    /*!
      Set the arg "key" with "value" in the source device.
    */
    int (*set_arg) (void *env, const char key[], const char value[]);
    
    /*!
      Return the size of the CD in logical block address (LBA) units.
    */
    uint32_t (*stat_size) (void *env);

  } cdio_funcs;


  /*! Implementation of CdIo type */
  struct _CdIo {
    driver_id_t driver_id; /**< Particular driver opened. */
    cdio_funcs op;         /**< driver-specific routines handling
			      implementation*/
    void *env;             /**< environment. Passed to routine above. */
  };

  /* This is used in drivers that must keep their own internal 
     position pointer for doing seeks. Stream-based drivers (like bincue,
     nrg, toc, network) would use this. 
   */
  typedef struct 
  {
    off_t   buff_offset;      /* buffer offset in disk-image seeks. */
    track_t index;            /* Current track index in tocent. */
    lba_t   lba;              /* Current LBA */
  } internal_position_t;
  
  CdIo * cdio_new (generic_img_private_t *p_env, cdio_funcs *funcs);

  /* The below structure describes a specific CD Input driver  */
  typedef struct 
  {
    driver_id_t  id;
    unsigned int flags;
    const char  *name;
    const char  *describe;
    bool (*have_driver) (void); 
    CdIo *(*driver_open) (const char *psz_source_name); 
    CdIo *(*driver_open_am) (const char *psz_source_name, 
			     const char *psz_access_mode); 
    char *(*get_default_device) (void); 
    bool (*is_device) (const char *psz_source_name);
    char **(*get_devices) (void);
  } CdIo_driver_t;

  /* The below array gives of the drivers that are currently available for 
     on a particular host. */
  extern CdIo_driver_t CdIo_driver[CDIO_MAX_DRIVER];

  /* The last valid entry of Cdio_driver. -1 means uninitialzed. -2 
     means some sort of error.
   */
  extern int CdIo_last_driver; 

  /* The below array gives all drivers that can possibly appear.
     on a particular host. */
  extern CdIo_driver_t CdIo_all_drivers[CDIO_MAX_DRIVER+1];

  /*! 
    Add/allocate a drive to the end of drives. 
    Use cdio_free_device_list() to free this device_list.
  */
  void cdio_add_device_list(char **device_list[], const char *drive, 
			    unsigned int *i_drives);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* __CDIO_PRIVATE_H__ */
