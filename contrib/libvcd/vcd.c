/*
    $Id: vcd.c,v 1.4 2006/12/08 16:26:10 mshopf Exp $

    Copyright (C) 2000, 2004 Herbert Valerio Riedel <hvr@gnu.org>

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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <math.h>

#include <cdio/cdio.h>
#include <cdio/iso9660.h>

/* public headers */
#include <libvcd/types.h>
#include <libvcd/info.h>

#include <libvcd/files.h>
#include <libvcd/sector.h>
#include <libvcd/logging.h>

/* Private headers */
#include "assert.h"
#include "dict.h"
#include "directory.h"
#include "obj.h"
#include "pbc.h"
#include "salloc.h"
#include "util.h"
#include "vcd.h"

static const char _rcsid[] = "$Id: vcd.c,v 1.4 2006/12/08 16:26:10 mshopf Exp $";

static const char zero[CDIO_CD_FRAMESIZE_RAW] = { 0, };

#define DEFAULT_ISO_PREPARER_ID       "GNU VCDImager " VERSION " " HOST_ARCH

/* exported private functions
 */

mpeg_sequence_t *
_vcd_obj_get_sequence_by_id (VcdObj *obj, const char sequence_id[])
{
  CdioListNode *node;

  vcd_assert (sequence_id != NULL);
  vcd_assert (obj != NULL);

  _CDIO_LIST_FOREACH (node, obj->mpeg_sequence_list)
    {
      mpeg_sequence_t *_sequence = _cdio_list_node_data (node);

      if (_sequence->id && !strcmp (sequence_id, _sequence->id))
        return _sequence;
    }

  return NULL;
}

mpeg_sequence_t *
_vcd_obj_get_sequence_by_entry_id (VcdObj *obj, const char entry_id[])
{
  CdioListNode *node;

  vcd_assert (entry_id != NULL);
  vcd_assert (obj != NULL);

  _CDIO_LIST_FOREACH (node, obj->mpeg_sequence_list)
    {
      mpeg_sequence_t *_sequence = _cdio_list_node_data (node);
      CdioListNode *node2;

      /* default entry point */
      if (_sequence->default_entry_id 
          && !strcmp (entry_id, _sequence->default_entry_id))
        return _sequence;

      /* additional entry points */
      _CDIO_LIST_FOREACH (node2, _sequence->entry_list)
	{
	  entry_t *_entry = _cdio_list_node_data (node2);

	  if (_entry->id 
              && !strcmp (entry_id, _entry->id))
	    return _sequence;
	}
    }

  /* not found */

  return NULL;
}

mpeg_segment_t *
_vcd_obj_get_segment_by_id (VcdObj *obj, const char segment_id[])
{
  CdioListNode *node;

  vcd_assert (segment_id != NULL);
  vcd_assert (obj != NULL);

  _CDIO_LIST_FOREACH (node, obj->mpeg_segment_list)
    {
      mpeg_segment_t *_segment = _cdio_list_node_data (node);

      if (_segment->id && !strcmp (segment_id, _segment->id))
        return _segment;
    }

  return NULL;
}

bool
_vcd_obj_has_cap_p (const VcdObj *obj, enum vcd_capability_t capability)
{
  switch (capability)
    {
    case _CAP_VALID:
      switch (obj->type)
        {
        case VCD_TYPE_VCD:
        case VCD_TYPE_VCD11:
        case VCD_TYPE_VCD2:
        case VCD_TYPE_SVCD:
        case VCD_TYPE_HQVCD:
          return true;
          break;

        case VCD_TYPE_INVALID:
          return false;
          break;
        }
      break;

    case _CAP_MPEG2:
      switch (obj->type)
        {
        case VCD_TYPE_VCD:
        case VCD_TYPE_VCD11:
        case VCD_TYPE_VCD2:
        case VCD_TYPE_INVALID:
          return false;
          break;

        case VCD_TYPE_SVCD:
        case VCD_TYPE_HQVCD:
          return true;
          break;
        }
      break;

    case _CAP_PBC:
      switch (obj->type)
        {
        case VCD_TYPE_VCD:
        case VCD_TYPE_VCD11:
        case VCD_TYPE_INVALID:
          return false;
          break;

        case VCD_TYPE_VCD2:
        case VCD_TYPE_SVCD:
        case VCD_TYPE_HQVCD:
          return true;
          break;
        }
      break;

    case _CAP_PBC_X:
      switch (obj->type)
        {
        case VCD_TYPE_VCD:
        case VCD_TYPE_VCD11:
        case VCD_TYPE_INVALID:
        case VCD_TYPE_SVCD:
        case VCD_TYPE_HQVCD:
          return false;
          break;

        case VCD_TYPE_VCD2:
          return true;
          break;
        }
      break;

    case _CAP_4C_SVCD:
      switch (obj->type)
        {
        case VCD_TYPE_VCD:
        case VCD_TYPE_VCD11:
        case VCD_TYPE_INVALID:
        case VCD_TYPE_VCD2:
          return false;
          break;

        case VCD_TYPE_SVCD:
        case VCD_TYPE_HQVCD:
          return true;
          break;
        }
      break;

    case _CAP_PAL_BITS:
      return _vcd_obj_has_cap_p (obj, _CAP_PBC); /* for now */
      break;

    case _CAP_MPEG1:
      return !_vcd_obj_has_cap_p (obj, _CAP_MPEG2); /* for now */
      break;

    case _CAP_TRACK_MARGINS:
      return !_vcd_obj_has_cap_p (obj, _CAP_MPEG2); /* for now */
      break;
    }

  vcd_assert_not_reached ();
  return false;
}

/*
 * public methods
 */ 

VcdObj *
vcd_obj_new (vcd_type_t vcd_type)
{
  VcdObj *new_obj = NULL;
  static bool _first = true;
  
  if (_first)
    {
#if defined(_DEVELOPMENT_)      
      vcd_warn ("initializing libvcd %s [%s]", VERSION, HOST_ARCH);
      vcd_warn (" ");
      vcd_warn (" this is the UNSTABLE development branch!");
      vcd_warn (" use only if you know what you are doing");
      vcd_warn (" see http://www.hvrlab.org/~hvr/vcdimager/ for more information");
      vcd_warn (" ");
#else
      vcd_debug ("initializing libvcd %s [%s]", VERSION, HOST_ARCH);
#endif
      _first = false;
    }

  new_obj = _vcd_malloc (sizeof (VcdObj));
  new_obj->type = vcd_type;

  if (!_vcd_obj_has_cap_p (new_obj, _CAP_VALID))
    {
      vcd_error ("VCD type not supported");
      free (new_obj);
      return NULL;
    }

  if (vcd_type == VCD_TYPE_VCD)
    vcd_warn ("VCD 1.0 support is experimental -- user feedback needed!");

  new_obj->iso_volume_label = strdup ("");
  new_obj->iso_publisher_id = strdup ("");
  new_obj->iso_application_id = strdup ("");
  new_obj->iso_preparer_id = _vcd_strdup_upper (DEFAULT_ISO_PREPARER_ID);
  new_obj->info_album_id = strdup ("");
  new_obj->info_volume_count = 1;
  new_obj->info_volume_number = 1;

  new_obj->custom_file_list = _cdio_list_new ();
  new_obj->custom_dir_list = _cdio_list_new ();


  new_obj->mpeg_sequence_list = _cdio_list_new ();

  new_obj->mpeg_segment_list = _cdio_list_new ();

  new_obj->pbc_list = _cdio_list_new ();

  /* gap's defined by IEC-10149 / ECMA-130 */

  /* pre-gap's for tracks but the first one */
  new_obj->track_pregap = CDIO_PREGAP_SECTORS; 
  /* post-gap after last track */
  new_obj->leadout_pregap = CDIO_POSTGAP_SECTORS; 

  if (_vcd_obj_has_cap_p (new_obj, _CAP_TRACK_MARGINS))
    {
      new_obj->track_front_margin = 30;
      new_obj->track_rear_margin = 45;
    }
  else
    {
      new_obj->track_front_margin = 0;
      new_obj->track_rear_margin = 0;
    }

  return new_obj;
}

int 
vcd_obj_remove_item (VcdObj *obj, const char id[])
{
  vcd_warn ("vcd_obj_remove_item('%s') not implemented yet!", id);

  return -1;
}

static void
_vcd_obj_remove_mpeg_track (VcdObj *obj, int track_id)
{
  int length;
  mpeg_sequence_t *track = NULL;
  CdioListNode *node = NULL;

  vcd_assert (track_id >= 0);

  node = _vcd_list_at (obj->mpeg_sequence_list, track_id);
  
  vcd_assert (node != NULL);

  track = (mpeg_sequence_t *) _cdio_list_node_data (node);

  vcd_mpeg_source_destroy (track->source, true);

  length = track->info->packets;
  length += obj->track_pregap + obj->track_front_margin + 0 + obj->track_rear_margin;

  /* fixup offsets */
  {
    CdioListNode *node2 = node;
    while ((node2 = _cdio_list_node_next (node2)) != NULL)
      ((mpeg_sequence_t *) _cdio_list_node_data (node))->relative_start_extent -= length;
  }

  obj->relative_end_extent -= length;

  /* shift up */
  _cdio_list_node_free (node, true);
}

int
vcd_obj_append_segment_play_item (VcdObj *obj, VcdMpegSource *mpeg_source,
                                  const char item_id[])
{
  mpeg_segment_t *segment = NULL;

  vcd_assert (obj != NULL);
  vcd_assert (mpeg_source != NULL);

  if (!_vcd_obj_has_cap_p (obj, _CAP_PBC))
    {
      vcd_error ("segment play items not supported for this vcd type");
      return -1;
    }

  if (!item_id)
    {
      vcd_error ("no id given for segment play item");
      return -1;
    }

  if (_vcd_pbc_lookup (obj, item_id))
    {
      vcd_error ("item id (%s) exists already", item_id);
      return -1;
    }

  vcd_info ("scanning mpeg segment item #%d for scanpoints...", 
            _cdio_list_length (obj->mpeg_segment_list));

  vcd_mpeg_source_scan (mpeg_source, !obj->relaxed_aps,
                        obj->update_scan_offsets, NULL, NULL);

  if (vcd_mpeg_source_get_info (mpeg_source)->packets == 0)
    {
      vcd_error ("mpeg is empty?");
      return -1;
    }

  /* create list node */

  segment = _vcd_malloc (sizeof (mpeg_sequence_t));

  segment->source = mpeg_source;

  segment->id = strdup (item_id);
 
  segment->info = vcd_mpeg_source_get_info (mpeg_source);
  segment->segment_count = _vcd_len2blocks (segment->info->packets, 150);

  segment->pause_list = _cdio_list_new ();

  vcd_debug ("SPI length is %d sector(s), allocated %d segment(s)",
             segment->info->packets,
             segment->segment_count);

  _cdio_list_append (obj->mpeg_segment_list, segment);

  return 0;
}

int
vcd_obj_append_sequence_play_item (VcdObj *obj, VcdMpegSource *mpeg_source,
                                   const char item_id[],
                                   const char default_entry_id[])
{
  unsigned length;
  mpeg_sequence_t *sequence = NULL;
  int track_no = _cdio_list_length (obj->mpeg_sequence_list);

  vcd_assert (obj != NULL);
  vcd_assert (mpeg_source != NULL);

  if (item_id && _vcd_pbc_lookup (obj, item_id))
    {
      vcd_error ("item id (%s) exist already", item_id);
      return -1;
    }

  if (default_entry_id && _vcd_pbc_lookup (obj, default_entry_id))
    {
      vcd_error ("default entry id (%s) exist already", default_entry_id);
      return -1;
    }

  if (default_entry_id && item_id && !strcmp (item_id, default_entry_id))
    {
      vcd_error ("default entry id == item id (%s)", item_id);
      return -1;
    }

  vcd_info ("scanning mpeg sequence item #%d for scanpoints...", track_no);
  vcd_mpeg_source_scan (mpeg_source, !obj->relaxed_aps,
                        obj->update_scan_offsets, NULL, NULL);

  sequence = _vcd_malloc (sizeof (mpeg_sequence_t));

  sequence->source = mpeg_source;

  if (item_id)
    sequence->id = strdup (item_id);

  if (default_entry_id)
    sequence->default_entry_id = strdup (default_entry_id);
  
  sequence->info = vcd_mpeg_source_get_info (mpeg_source);
  length = sequence->info->packets;

  sequence->entry_list = _cdio_list_new ();
  sequence->pause_list = _cdio_list_new ();

  obj->relative_end_extent += obj->track_pregap;
  sequence->relative_start_extent = obj->relative_end_extent;

  obj->relative_end_extent += obj->track_front_margin + length + obj->track_rear_margin;

  /* sanity checks */

  if (length < 75)
    vcd_warn ("mpeg stream shorter than 75 sectors");

  if (!_vcd_obj_has_cap_p (obj, _CAP_PAL_BITS)
      && vcd_mpeg_get_norm (&sequence->info->shdr[0]) != MPEG_NORM_FILM
      && vcd_mpeg_get_norm (&sequence->info->shdr[0]) != MPEG_NORM_NTSC)
    vcd_warn ("VCD 1.x should contain only NTSC/FILM video (may work with PAL nevertheless)");

  if (!_vcd_obj_has_cap_p (obj, _CAP_MPEG1)
      && sequence->info->version == MPEG_VERS_MPEG1)
    vcd_warn ("this VCD type should not contain MPEG1 streams");

  if (!_vcd_obj_has_cap_p (obj, _CAP_MPEG2)
      && sequence->info->version == MPEG_VERS_MPEG2)
    vcd_warn ("this VCD type should not contain MPEG2 streams");

  if (!sequence->info->shdr[0].seen
      || sequence->info->shdr[1].seen
      || sequence->info->shdr[2].seen)
    vcd_warn ("sequence items should contain a motion video stream!");

  {
    int i;

    for (i = 0; i < 3; i++)
      {
        if (sequence->info->ahdr[i].seen)
          {
            if (i && !_vcd_obj_has_cap_p (obj, _CAP_MPEG2))
              vcd_warn ("audio stream #%d not supported by this VCD type", i);

            if (sequence->info->ahdr[i].sampfreq != 44100)
              vcd_warn ("audio stream #%d has sampling frequency %d Hz (should be 44100 Hz)", 
                        i, sequence->info->ahdr[i].sampfreq);

            if (sequence->info->ahdr[i].layer != 2)
              vcd_warn ("audio stream #%d is not layer II", i);

            if (_vcd_obj_has_cap_p (obj, _CAP_MPEG1) 
                && sequence->info->ahdr[i].bitrate != 224*1024)
              vcd_warn ("audio stream #%d has bitrate %d kbps (should be 224 kbps for this vcd type)",
                        i, sequence->info->ahdr[i].bitrate);
          }
        else if (!i && !_vcd_obj_has_cap_p (obj, _CAP_MPEG2))
          {
            vcd_warn ("this VCD type requires an audio stream to be present");
          }
      }
  }
    
  /* vcd_debug ("track# %d's detected playing time: %.2f seconds",  */
  /*            track_no, sequence->info->playing_time); */

  _cdio_list_append (obj->mpeg_sequence_list, sequence);

  return track_no;
}

static int
_pause_cmp (pause_t *ent1, pause_t *ent2)
{
  if (ent1->time < ent2->time)
    return -1;

  if (ent1->time > ent2->time)
    return 1;

  return 0;
}

int 
vcd_obj_add_sequence_pause (VcdObj *obj, const char sequence_id[], 
                            double pause_time, const char pause_id[])
{
  mpeg_sequence_t *_sequence;

  vcd_assert (obj != NULL);

  if (sequence_id)
    _sequence = _vcd_obj_get_sequence_by_id (obj, sequence_id);
  else
    _sequence = 
      _cdio_list_node_data (_cdio_list_end (obj->mpeg_sequence_list));

  if (!_sequence)
    {
      vcd_error ("sequence id `%s' not found", sequence_id);
      return -1;
    }

  if (pause_id)
    vcd_warn ("pause id ignored...");

  {
    pause_t *_pause = _vcd_malloc (sizeof (pause_t));

    if (pause_id)
      _pause->id = strdup (pause_id);
    _pause->time = pause_time;

    _cdio_list_append (_sequence->pause_list, _pause);
  }

  _vcd_list_sort (_sequence->pause_list, 
                  (_cdio_list_cmp_func) _pause_cmp);

  vcd_debug ("added autopause point at %f", pause_time);

  return 0;
}

int 
vcd_obj_add_segment_pause (VcdObj *obj, const char segment_id[], 
                            double pause_time, const char pause_id[])
{
  mpeg_segment_t *_segment;

  vcd_assert (obj != NULL);

  if (segment_id)
    _segment = _vcd_obj_get_segment_by_id (obj, segment_id);
  else
    _segment = _cdio_list_node_data (_cdio_list_end (obj->mpeg_segment_list));

  if (!_segment)
    {
      vcd_error ("segment id `%s' not found", segment_id);
      return -1;
    }

  if (pause_id)
    vcd_warn ("pause id ignored...");

  {
    pause_t *_pause = _vcd_malloc (sizeof (pause_t));

    if (pause_id)
      _pause->id = strdup (pause_id);
    _pause->time = pause_time;

    _cdio_list_append (_segment->pause_list, _pause);
  }

  _vcd_list_sort (_segment->pause_list, 
                  (_cdio_list_cmp_func) _pause_cmp);

  vcd_debug ("added autopause point at %f", pause_time);

  return 0;
}

static int
_entry_cmp (entry_t *ent1, entry_t *ent2)
{
  if (ent1->time < ent2->time)
    return -1;

  if (ent1->time > ent2->time)
    return 1;

  return 0;
}

int 
vcd_obj_add_sequence_entry (VcdObj *obj, const char sequence_id[], 
                            double entry_time, const char entry_id[])
{
  mpeg_sequence_t *_sequence;

  vcd_assert (obj != NULL);

  if (sequence_id)
    _sequence = _vcd_obj_get_sequence_by_id (obj, sequence_id);
  else
    _sequence =
      _cdio_list_node_data (_cdio_list_end (obj->mpeg_sequence_list));

  if (!_sequence)
    {
      vcd_error ("sequence id `%s' not found", sequence_id);
      return -1;
    }

  if (_cdio_list_length (_sequence->entry_list) >= MAX_SEQ_ENTRIES)
    {
      vcd_error ("only %d entries per sequence allowed!", MAX_SEQ_ENTRIES);
      return -1;
    }

  if (entry_id && _vcd_pbc_lookup (obj, entry_id))
    {
      vcd_error ("item id (%s) exists already", entry_id);
      return -1;
    }

  {
    entry_t *_entry = _vcd_malloc (sizeof (entry_t));

    if (entry_id)
      _entry->id = strdup (entry_id);
    _entry->time = entry_time;

    _cdio_list_append (_sequence->entry_list, _entry);
  }

  _vcd_list_sort (_sequence->entry_list, 
                  (_cdio_list_cmp_func) _entry_cmp);

  return 0;
}

void 
vcd_obj_destroy (VcdObj *obj)
{
  CdioListNode *node;

  vcd_assert (obj != NULL);
  vcd_assert (!obj->in_output);

  free (obj->iso_volume_label);
  free (obj->iso_application_id);

  _CDIO_LIST_FOREACH (node, obj->custom_file_list)
    {
      custom_file_t *p = _cdio_list_node_data (node);
    
      free (p->iso_pathname);
    }

  _cdio_list_free (obj->custom_file_list, true);

  _cdio_list_free (obj->custom_dir_list, true);

  while (_cdio_list_length (obj->mpeg_sequence_list))
    _vcd_obj_remove_mpeg_track (obj, 0);
  _cdio_list_free (obj->mpeg_sequence_list, true);

  free (obj);
}

int 
vcd_obj_set_param_uint (VcdObj *obj, vcd_parm_t param, unsigned arg)
{
  vcd_assert (obj != NULL);

  switch (param) 
    {
    case VCD_PARM_VOLUME_COUNT:
      obj->info_volume_count = arg;
      if (!IN (obj->info_volume_count, 1, 65535))
        {
          obj->info_volume_count = CLAMP (obj->info_volume_count, 1, 65535);
          vcd_warn ("volume count out of range, clamping to range");
        }
      vcd_debug ("changed volume count to %u", obj->info_volume_count);
      break;

    case VCD_PARM_VOLUME_NUMBER:
      obj->info_volume_number = arg;
      if (!IN (obj->info_volume_number, 0, 65534))
        {
          obj->info_volume_number = CLAMP (obj->info_volume_number, 0, 65534);
          vcd_warn ("volume number out of range, clamping to range");
        }
      vcd_debug ("changed volume number to %u", obj->info_volume_number);
      break;

    case VCD_PARM_RESTRICTION:
      obj->info_restriction = arg;
      if (!IN (obj->info_restriction, 0, 3))
        {
          obj->info_restriction = CLAMP (obj->info_restriction, 0, 65534);
          vcd_warn ("restriction out of range, clamping to range");
        }
      vcd_debug ("changed restriction number to %u", obj->info_restriction);
      break;

    case VCD_PARM_LEADOUT_PREGAP:
      obj->leadout_pregap = arg;
      if (!IN (obj->leadout_pregap, 0, 300))
        {
          obj->leadout_pregap = CLAMP (obj->leadout_pregap, 0, 300);
          vcd_warn ("ledout pregap out of range, clamping to allowed range");
        }
      if (obj->leadout_pregap < CDIO_PREGAP_SECTORS)
        vcd_warn ("track leadout pregap set below %d sectors; created (s)vcd may be non-working", 
                  CDIO_PREGAP_SECTORS);

      vcd_debug ("changed leadout pregap to %u", obj->leadout_pregap);
      break;

    case VCD_PARM_TRACK_PREGAP:
      obj->track_pregap = arg;
      if (!IN (obj->track_pregap, 1, 300))
        {
          obj->track_pregap = CLAMP (obj->track_pregap, 1, 300);
          vcd_warn ("track pregap out of range, clamping to allowed range");
        }
      if (obj->track_pregap < CDIO_PREGAP_SECTORS)
        vcd_warn ("track pre gap set below %d sectors; created (S)VCD may be non-working", 
                  CDIO_PREGAP_SECTORS);
      vcd_debug ("changed track pregap to %u", obj->track_pregap);
      break;

    case VCD_PARM_TRACK_FRONT_MARGIN:
      obj->track_front_margin = arg;
      if (!IN (obj->track_front_margin, 0, CDIO_PREGAP_SECTORS))
        {
          obj->track_front_margin = CLAMP (obj->track_front_margin, 0, 
                                           CDIO_PREGAP_SECTORS);
          vcd_warn ("front margin out of range, clamping to allowed range");
        }
      if (_vcd_obj_has_cap_p (obj, _CAP_TRACK_MARGINS) 
          && obj->track_front_margin < 15)
        vcd_warn ("front margin set smaller than recommended (%d < 15 sectors) for disc type used",
                  obj->track_front_margin);

      vcd_debug ("changed front margin to %u", obj->track_front_margin);
      break;

    case VCD_PARM_TRACK_REAR_MARGIN:
      obj->track_rear_margin = arg;
      if (!IN (obj->track_rear_margin, 0, CDIO_POSTGAP_SECTORS))
        {
          obj->track_rear_margin = CLAMP (obj->track_rear_margin, 0, 
                                          CDIO_POSTGAP_SECTORS);
          vcd_warn ("rear margin out of range, clamping to allowed range");
        }
      if (_vcd_obj_has_cap_p (obj, _CAP_TRACK_MARGINS) 
          && obj->track_rear_margin < 15)
        vcd_warn ("rear margin set smaller than recommended (%d < 15 sectors) for disc type used",
                  obj->track_rear_margin);
      vcd_debug ("changed rear margin to %u", obj->track_rear_margin);
      break;

    default:
      vcd_assert_not_reached ();
      break;
    }

  return 0;
}

int 
vcd_obj_set_param_str (VcdObj *obj, vcd_parm_t param, const char *arg)
{
  vcd_assert (obj != NULL);
  vcd_assert (arg != NULL);

  switch (param) 
    {
    case VCD_PARM_VOLUME_ID:
      free (obj->iso_volume_label);
      obj->iso_volume_label = strdup (arg);
      if (strlen (obj->iso_volume_label) > 32)
        {
          obj->iso_volume_label[32] = '\0';
          vcd_warn ("Volume label too long, will be truncated");
        }
      vcd_debug ("changed volume label to `%s'", obj->iso_volume_label);
      break;

    case VCD_PARM_PUBLISHER_ID:
      free (obj->iso_publisher_id);
      obj->iso_publisher_id = strdup (arg);
      if (strlen (obj->iso_publisher_id) > 128)
        {
          obj->iso_publisher_id[128] = '\0';
          vcd_warn ("Publisher ID too long, will be truncated");
        }
      vcd_debug ("changed publisher id to `%s'", obj->iso_publisher_id);
      break;

    case VCD_PARM_PREPARER_ID:
      free (obj->iso_preparer_id);
      obj->iso_preparer_id = strdup (arg);
      if (strlen (obj->iso_preparer_id) > 128)
        {
          obj->iso_preparer_id[128] = '\0';
          vcd_warn ("Preparer ID too long, will be truncated");
        }
      vcd_debug ("changed preparer id to `%s'", obj->iso_preparer_id);
      break;

    case VCD_PARM_APPLICATION_ID:
      free (obj->iso_application_id);
      obj->iso_application_id = strdup (arg);
      if (strlen (obj->iso_application_id) > 128)
        {
          obj->iso_application_id[128] = '\0';
          vcd_warn ("Application ID too long, will be truncated");
        }
      vcd_debug ("changed application id to `%s'", obj->iso_application_id);
      break;
      
    case VCD_PARM_ALBUM_ID:
      free (obj->info_album_id);
      obj->info_album_id = strdup (arg);
      if (strlen (obj->info_album_id) > 16)
        {
          obj->info_album_id[16] = '\0';
          vcd_warn ("Album ID too long, will be truncated");
        }
      vcd_debug ("changed album id to `%s'", obj->info_album_id);
      break;

    default:
      vcd_assert_not_reached ();
      break;
    }

  return 0;
}

int 
vcd_obj_set_param_bool (VcdObj *obj, vcd_parm_t param, bool arg)
{
  vcd_assert (obj != NULL);

  switch (param) 
    {
    case VCD_PARM_RELAXED_APS:
      obj->relaxed_aps = arg ? true : false;
      vcd_debug ("changing 'relaxed aps' to %d", obj->relaxed_aps);
      break;

    case VCD_PARM_NEXT_VOL_LID2:
      obj->info_use_lid2 = arg ? true : false;
      vcd_debug ("changing 'next volume use lid 2' to %d", obj->info_use_lid2);
      break;

    case VCD_PARM_NEXT_VOL_SEQ2:
      obj->info_use_seq2 = arg ? true : false;
      vcd_debug ("changing 'next volume use sequence 2' to %d", obj->info_use_seq2);
      break;

    case VCD_PARM_SVCD_VCD3_MPEGAV:
      if (obj->type == VCD_TYPE_SVCD)
        {
          if ((obj->svcd_vcd3_mpegav = arg ? true : false))
            vcd_warn ("!! enabling deprecated VCD3.0 MPEGAV folder --" 
                      " SVCD will not be IEC62107 compliant !!");
        }
      else
        vcd_error ("parameter not applicable for vcd type");
      break;

    case VCD_PARM_SVCD_VCD3_ENTRYSVD:
      if (obj->type == VCD_TYPE_SVCD)
        {
          if ((obj->svcd_vcd3_entrysvd = arg ? true : false))
            vcd_warn ("!! enabling deprecated VCD3.0 ENTRYSVD signature --" 
                      " SVCD will not be IEC62107 compliant !!");
        }
      else
        vcd_error ("parameter not applicable for vcd type");
      break;

    case VCD_PARM_SVCD_VCD3_TRACKSVD:
      if (obj->type == VCD_TYPE_SVCD)
        {
          if ((obj->svcd_vcd3_tracksvd = arg ? true : false))
            vcd_warn ("!! enabling deprecated VCD3.0 TRACK.SVD format --" 
                      " SVCD will not be IEC62107 compliant !!");
        }
      else
        vcd_error ("parameter not applicable for vcd type");
      break;

    case VCD_PARM_UPDATE_SCAN_OFFSETS:
      if (_vcd_obj_has_cap_p (obj, _CAP_4C_SVCD))
        {
          obj->update_scan_offsets = arg ? true : false;
          vcd_debug ("changing 'update scan offsets' to %d", obj->update_scan_offsets);
        }
      else
        vcd_error ("parameter not applicable for vcd type");
      break;

    case VCD_PARM_LEADOUT_PAUSE:
      vcd_warn ("use of 'leadout pause' is deprecated and may be removed in later releases;"
                " use 'leadout pregap' instead");
      vcd_obj_set_param_uint (obj, VCD_PARM_LEADOUT_PREGAP, 
                              (arg ? CDIO_PREGAP_SECTORS : 0));
      break;

    default:
      vcd_assert_not_reached ();
      break;
    }

  return 0;
}

int
vcd_obj_add_dir (VcdObj *obj, const char iso_pathname[])
{
  char *_iso_pathname;

  vcd_assert (obj != NULL);
  vcd_assert (iso_pathname != NULL);

  _iso_pathname = _vcd_strdup_upper (iso_pathname);
    
  if (!iso9660_dirname_valid_p (_iso_pathname))
    {
      vcd_error("pathname `%s' is not a valid iso pathname", 
                _iso_pathname);
      free (_iso_pathname);
      return 1;
    }

  _cdio_list_append (obj->custom_dir_list, _iso_pathname);

  _vcd_list_sort (obj->custom_dir_list, 
                  (_cdio_list_cmp_func) strcmp);

  return 0;
}

int
vcd_obj_add_file (VcdObj *obj, const char iso_pathname[],
                  VcdDataSource *file, bool raw_flag)
{
  uint32_t size = 0, sectors = 0;
 
  vcd_assert (obj != NULL);
  vcd_assert (file != NULL);
  vcd_assert (iso_pathname != NULL);
  vcd_assert (strlen (iso_pathname) > 0);
  vcd_assert (file != NULL);

  size = vcd_data_source_stat (file);

  /* close file to save file descriptors */
  vcd_data_source_close (file);

  if (raw_flag)
    {
      if (!size)
        {
          vcd_error("raw mode2 file must not be empty\n");
          return 1;
        }

      sectors = size / M2RAW_SECTOR_SIZE;

      if (size % M2RAW_SECTOR_SIZE)
        {
          vcd_error("raw mode2 file must have size multiple of %d \n", 
                    M2RAW_SECTOR_SIZE);
          return 1;
        }
    }
  else
    sectors = _vcd_len2blocks (size, CDIO_CD_FRAMESIZE);

  {
    custom_file_t *p;
    char *_iso_pathname = _vcd_strdup_upper (iso_pathname);
    
    if (!iso9660_pathname_valid_p (_iso_pathname))
      {
        vcd_error("pathname `%s' is not a valid iso pathname", 
                  _iso_pathname);
        free (_iso_pathname);
        return 1;
      }

    p = _vcd_malloc (sizeof (custom_file_t));
    
    p->file = file;
    p->iso_pathname = _iso_pathname;
    p->raw_flag = raw_flag;
  
    p->size = size;
    p->start_extent = 0;
    p->sectors = sectors;

    _cdio_list_append (obj->custom_file_list, p);
  }

  return 0;
}

static void
_finalize_vcd_iso_track_allocation (VcdObj *obj)
{
  int n;
  CdioListNode *node;

  uint32_t dir_secs = SECTOR_NIL;

  _dict_clean (obj);

  /* pre-alloc 16 blocks of ISO9660 required silence */
  if (_vcd_salloc (obj->iso_bitmap, 0, 16) == SECTOR_NIL)
    vcd_assert_not_reached ();

  /* keep karaoke sectors blank -- well... guess I'm too paranoid :) */
  if (_vcd_salloc (obj->iso_bitmap, 75, 75) == SECTOR_NIL) 
    vcd_assert_not_reached ();

  /* pre-alloc descriptors, PVD */
  _dict_insert (obj, "pvd", ISO_PVD_SECTOR, 1, SM_EOR);           /* EOR */
  /* EVD */
  _dict_insert (obj, "evd", ISO_EVD_SECTOR, 1, SM_EOR|SM_EOF);    /* EOR+EOF */

  /* reserve for iso directory */
  dir_secs = _vcd_salloc (obj->iso_bitmap, 18, 75-18);

  /* VCD information area */
  
  _dict_insert (obj, "info", INFO_VCD_SECTOR, 1, SM_EOF);              /* INFO.VCD */           /* EOF */
  _dict_insert (obj, "entries", ENTRIES_VCD_SECTOR, 1, SM_EOF);        /* ENTRIES.VCD */        /* EOF */

  /* PBC */

  if (_vcd_pbc_available (obj))
    {
      _dict_insert (obj, "lot", LOT_VCD_SECTOR, LOT_VCD_SIZE, SM_EOF); /* LOT.VCD */            /* EOF */
      _dict_insert (obj, "psd", PSD_VCD_SECTOR, 
                    _vcd_len2blocks (get_psd_size (obj, false), ISO_BLOCKSIZE), SM_EOF); /* PSD.VCD */ /* EOF */
    }

  if (_vcd_obj_has_cap_p (obj, _CAP_4C_SVCD))
    {
      _dict_insert (obj, "tracks", SECTOR_NIL, 1, SM_EOF);      /* TRACKS.SVD */
      _dict_insert (obj, "search", SECTOR_NIL, 
                    _vcd_len2blocks (get_search_dat_size (obj), ISO_BLOCKSIZE), SM_EOF); /* SEARCH.DAT */

      vcd_assert (_dict_get_bykey (obj, "tracks")->sector > INFO_VCD_SECTOR);
      vcd_assert (_dict_get_bykey (obj, "search")->sector > INFO_VCD_SECTOR);
    }

  /* done with primary information area */

  obj->mpeg_segment_start_extent = 
    _vcd_len2blocks (_vcd_salloc_get_highest (obj->iso_bitmap) + 1, 75) * 75;

  /* salloc up to end of vcd sector */
  for(n = 0;n < obj->mpeg_segment_start_extent;n++) 
    _vcd_salloc (obj->iso_bitmap, n, 1);
  
  vcd_assert (_vcd_salloc_get_highest (obj->iso_bitmap) + 1 == obj->mpeg_segment_start_extent);

  /* insert segments */

  _CDIO_LIST_FOREACH (node, obj->mpeg_segment_list)
    {
      mpeg_segment_t *_segment = _cdio_list_node_data (node);
      
      _segment->start_extent = 
        _vcd_salloc (obj->iso_bitmap, SECTOR_NIL, 
                     _segment->segment_count * VCDINFO_SEGMENT_SECTOR_SIZE);

      vcd_assert (_segment->start_extent % 75 == 0);
      vcd_assert (_vcd_salloc_get_highest (obj->iso_bitmap) + 1 
                  == _segment->start_extent 
                  + _segment->segment_count * VCDINFO_SEGMENT_SECTOR_SIZE);
    }

  obj->ext_file_start_extent = _vcd_salloc_get_highest (obj->iso_bitmap) + 1;

  vcd_assert (obj->ext_file_start_extent % 75 == 0);

  /* go on with EXT area */

  if (_vcd_obj_has_cap_p (obj, _CAP_4C_SVCD))
    {
      _dict_insert (obj, "scandata", SECTOR_NIL, 
                    _vcd_len2blocks (get_scandata_dat_size (obj),
                                     ISO_BLOCKSIZE),
                    SM_EOF);
    }

  if (_vcd_obj_has_cap_p (obj, _CAP_PBC_X)
      &&_vcd_pbc_available (obj))
    {
      _dict_insert (obj, "lot_x", SECTOR_NIL, LOT_VCD_SIZE, SM_EOF);
      
      _dict_insert (obj, "psd_x", SECTOR_NIL,
                    _vcd_len2blocks (get_psd_size (obj, true), ISO_BLOCKSIZE),
                    SM_EOF);
    }


  obj->custom_file_start_extent =
    _vcd_salloc_get_highest (obj->iso_bitmap) + 1;

  /* now for the custom files */

  _CDIO_LIST_FOREACH (node, obj->custom_file_list)
    {
      custom_file_t *p = _cdio_list_node_data (node);
      
      if (p->sectors)
        {
          p->start_extent =
            _vcd_salloc(obj->iso_bitmap, SECTOR_NIL, p->sectors);
          vcd_assert (p->start_extent != SECTOR_NIL);
        }
      else /* zero sized files -- set dummy extent */
        p->start_extent = obj->custom_file_start_extent;
    }

  /* calculate iso size -- after this point no sector shall be
     allocated anymore */

  obj->iso_size =
    MAX (MIN_ISO_SIZE, _vcd_salloc_get_highest (obj->iso_bitmap) + 1);

  vcd_debug ("iso9660: highest alloced sector is %lu (using %d as isosize)", 
             (unsigned long int) _vcd_salloc_get_highest (obj->iso_bitmap), 
             obj->iso_size);

  /* after this point the ISO9660's size is frozen */
}

static void
_finalize_vcd_iso_track_filesystem (VcdObj *obj)
{
  int n;
  CdioListNode *node;

  /* create filesystem entries */

  switch (obj->type) {
  case VCD_TYPE_VCD:
  case VCD_TYPE_VCD11:
  case VCD_TYPE_VCD2:
    /* add only necessary directories! */
    /* _vcd_directory_mkdir (obj->dir, "CDDA"); */
    /* _vcd_directory_mkdir (obj->dir, "CDI"); */
    _vcd_directory_mkdir (obj->dir, "EXT");
    /* _vcd_directory_mkdir (obj->dir, "KARAOKE"); */
    _vcd_directory_mkdir (obj->dir, "MPEGAV");
    _vcd_directory_mkdir (obj->dir, "VCD");

    /* add segment dir only when there are actually segment play items */
    if (_cdio_list_length (obj->mpeg_segment_list))
      _vcd_directory_mkdir (obj->dir, "SEGMENT");

    _vcd_directory_mkfile (obj->dir, "VCD/ENTRIES.VCD", 
                           _dict_get_bykey (obj, "entries")->sector, 
                           ISO_BLOCKSIZE, false, 0);    
    _vcd_directory_mkfile (obj->dir, "VCD/INFO.VCD",
                           _dict_get_bykey (obj, "info")->sector, 
                           ISO_BLOCKSIZE, false, 0);

    /* only for vcd2.0 */
    if (_vcd_pbc_available (obj))
      {
        _vcd_directory_mkfile (obj->dir, "VCD/LOT.VCD", 
                               _dict_get_bykey (obj, "lot")->sector, 
                               ISO_BLOCKSIZE*LOT_VCD_SIZE, false, 0);
        _vcd_directory_mkfile (obj->dir, "VCD/PSD.VCD", 
                               _dict_get_bykey (obj, "psd")->sector, 
                               get_psd_size (obj, false), false, 0);
      }
    break;

  case VCD_TYPE_SVCD:
  case VCD_TYPE_HQVCD:
    _vcd_directory_mkdir (obj->dir, "EXT");

    if (!obj->svcd_vcd3_mpegav)
      _vcd_directory_mkdir (obj->dir, "MPEG2");
    else
      {
        vcd_warn ("adding MPEGAV dir for *DEPRECATED* SVCD VCD30 mode");
        _vcd_directory_mkdir (obj->dir, "MPEGAV");
      }

    /* add segment dir only when there are actually segment play items */
    if (_cdio_list_length (obj->mpeg_segment_list))
      _vcd_directory_mkdir (obj->dir, "SEGMENT");

    _vcd_directory_mkdir (obj->dir, "SVCD");

    _vcd_directory_mkfile (obj->dir, "SVCD/ENTRIES.SVD",
                           _dict_get_bykey (obj, "entries")->sector, 
                           ISO_BLOCKSIZE, false, 0);    
    _vcd_directory_mkfile (obj->dir, "SVCD/INFO.SVD",
                           _dict_get_bykey (obj, "info")->sector, 
                           ISO_BLOCKSIZE, false, 0);

    if (_vcd_pbc_available (obj))
      {
        _vcd_directory_mkfile (obj->dir, "SVCD/LOT.SVD",
                               _dict_get_bykey (obj, "lot")->sector, 
                               ISO_BLOCKSIZE*LOT_VCD_SIZE, false, 0);
        _vcd_directory_mkfile (obj->dir, "SVCD/PSD.SVD", 
                               _dict_get_bykey (obj, "psd")->sector, 
                               get_psd_size (obj, false), false, 0);
      }

    _vcd_directory_mkfile (obj->dir, "SVCD/SEARCH.DAT", 
                           _dict_get_bykey (obj, "search")->sector, 
                           get_search_dat_size (obj), false, 0);
    _vcd_directory_mkfile (obj->dir, "SVCD/TRACKS.SVD",
                           _dict_get_bykey (obj, "tracks")->sector, 
                           ISO_BLOCKSIZE, false, 0);
    break;

  default:
    vcd_assert_not_reached ();
    break;
  }

  /* SEGMENTS */

  n = 1;
  _CDIO_LIST_FOREACH (node, obj->mpeg_segment_list)
    {
      mpeg_segment_t *segment = _cdio_list_node_data (node);
      char segment_pathname[128] = { 0, };
      const char *fmt = NULL;
      uint8_t fnum = 0;

      switch (obj->type) 
        {
        case VCD_TYPE_VCD2:
          fmt = "SEGMENT/ITEM%4.4d.DAT";
          fnum = 1;
          break;
        case VCD_TYPE_SVCD:
        case VCD_TYPE_HQVCD:
          fmt = "SEGMENT/ITEM%4.4d.MPG";
          fnum = 0;
          break;
        default:
          vcd_assert_not_reached ();
        }
      
      snprintf (segment_pathname, sizeof (segment_pathname), fmt, n);
        
      _vcd_directory_mkfile (obj->dir, segment_pathname, segment->start_extent,
                             segment->info->packets * ISO_BLOCKSIZE,
                             true, fnum);

      vcd_assert (n <= MAX_SEGMENTS);

      n += segment->segment_count;
    }

  /* EXT files */

  if (_vcd_obj_has_cap_p (obj, _CAP_PBC_X)
      &&_vcd_pbc_available (obj))
    {
      /* psd_x -- extended PSD */
      _vcd_directory_mkfile (obj->dir, "EXT/PSD_X.VCD",
                             _dict_get_bykey (obj, "psd_x")->sector, 
                             get_psd_size (obj, true), false, 1);

      /* lot_x -- extended LOT */
      _vcd_directory_mkfile (obj->dir, "EXT/LOT_X.VCD",
                             _dict_get_bykey (obj, "lot_x")->sector, 
                             ISO_BLOCKSIZE*LOT_VCD_SIZE, false, 1);

      vcd_assert (obj->type == VCD_TYPE_VCD2);
    }

  if (_vcd_obj_has_cap_p (obj, _CAP_4C_SVCD))
    {
      /* scandata.dat -- scanpoints */
      _vcd_directory_mkfile (obj->dir, "EXT/SCANDATA.DAT",
                             _dict_get_bykey (obj, "scandata")->sector, 
                             get_scandata_dat_size (obj), false, 0);
    }

  /* custom files/dirs */
  _CDIO_LIST_FOREACH (node, obj->custom_dir_list)
    {
      char *p = _cdio_list_node_data (node);
      _vcd_directory_mkdir (obj->dir, p);
    }

  _CDIO_LIST_FOREACH (node, obj->custom_file_list)
    {
      custom_file_t *p = _cdio_list_node_data (node);

      _vcd_directory_mkfile (obj->dir, p->iso_pathname, p->start_extent,
                             (p->raw_flag 
                              ? (ISO_BLOCKSIZE * (p->size / M2RAW_SECTOR_SIZE))
                              : p->size), 
                             p->raw_flag, 1);
    }


  n = 0;
  _CDIO_LIST_FOREACH (node, obj->mpeg_sequence_list)
    {
      char avseq_pathname[128] = { 0, };
      const char *fmt = NULL;
      mpeg_sequence_t *_sequence = _cdio_list_node_data (node);
      uint32_t extent = _sequence->relative_start_extent;
      uint8_t file_num = 0;
      
      extent += obj->iso_size;

      switch (obj->type) 
        {
        case VCD_TYPE_VCD:
          fmt = "MPEGAV/MUSIC%2.2d.DAT";
          file_num = n + 1;
          break;

        case VCD_TYPE_VCD11:
        case VCD_TYPE_VCD2:
          fmt = "MPEGAV/AVSEQ%2.2d.DAT";
          file_num = n + 1;
          break;
        case VCD_TYPE_SVCD:
        case VCD_TYPE_HQVCD:
          fmt = "MPEG2/AVSEQ%2.2d.MPG";
          file_num = 0;

          /* if vcd3 compat mode, override */
          if (obj->svcd_vcd3_mpegav)
            {
              fmt = "MPEGAV/AVSEQ%2.2d.MPG";
              file_num = n + 1;
            }
            
          break;
        default:
          vcd_assert_not_reached ();
        }

      vcd_assert (n < 98);
      
      snprintf (avseq_pathname, sizeof (avseq_pathname), fmt, n + 1);
      
      /* file entry contains front margin, mpeg stream and rear margin */
      _vcd_directory_mkfile (obj->dir, avseq_pathname, extent,
                             (obj->track_front_margin 
                              + _sequence->info->packets
                              + obj->track_rear_margin) * ISO_BLOCKSIZE,
                             true, file_num);

      n++;
    }

  /* register isofs dir structures */
  {
    uint32_t dirs_size = _vcd_directory_get_size (obj->dir);
    
    /* be sure to stay out of information areas */

    switch (obj->type) 
      {
      case VCD_TYPE_VCD:
      case VCD_TYPE_VCD11:
      case VCD_TYPE_VCD2:
        /* karaoke area starts at 03:00 */
        if (16 + 2 + dirs_size + 2 >= 75) 
          vcd_error ("directory section to big for a VCD");
        break;

      case VCD_TYPE_SVCD:
      case VCD_TYPE_HQVCD:
        /* since no karaoke exists the next fixed area starts at 04:00 */
        if (16 + 2 + dirs_size + 2 >= 150) 
          vcd_error ("directory section to big for a SVCD");
        break;
      default:
        vcd_assert_not_reached ();
      }
    
    /* un-alloc small area */
    
    _vcd_salloc_free (obj->iso_bitmap, 18, dirs_size + 2);
    
    /* alloc it again! */

    _dict_insert (obj, "dir", 18, dirs_size, SM_EOR|SM_EOF);
    _dict_insert (obj, "ptl", 18 + dirs_size, 1, SM_EOR|SM_EOF);
    _dict_insert (obj, "ptm", 18 + dirs_size + 1, 1, SM_EOR|SM_EOF);
  }
}

static void
_finalize_vcd_iso_track (VcdObj *obj)
{
  _vcd_pbc_finalize (obj);
  _finalize_vcd_iso_track_allocation (obj);
  _finalize_vcd_iso_track_filesystem (obj);
}

static int
_callback_wrapper (VcdObj *obj, int force)
{
  const int cb_frequency = 75;

  if (obj->last_cb_call + cb_frequency > obj->sectors_written && !force)
    return 0;

  obj->last_cb_call = obj->sectors_written;

  if (obj->progress_callback) {
    progress_info_t _pi;

    _pi.sectors_written = obj->sectors_written;
    _pi.total_sectors = obj->relative_end_extent + obj->iso_size; 
    _pi.in_track = obj->in_track;
    _pi.total_tracks = _cdio_list_length (obj->mpeg_sequence_list) + 1;

    return obj->progress_callback (&_pi, obj->callback_user_data);
  }
  else
    return 0;
}

static int
_write_m2_image_sector (VcdObj *obj, const void *data, uint32_t extent,
                        uint8_t fnum, uint8_t cnum, uint8_t sm, uint8_t ci) 
{
  char buf[CDIO_CD_FRAMESIZE_RAW] = { 0, };

  vcd_assert (extent == obj->sectors_written);

  _vcd_make_mode2(buf, data, extent, fnum, cnum, sm, ci);

  vcd_image_sink_write (obj->image_sink, buf, extent);
  
  obj->sectors_written++;

  return _callback_wrapper (obj, false);
}

static int
_write_m2_raw_image_sector (VcdObj *obj, const void *data, uint32_t extent)
{
  char buf[CDIO_CD_FRAMESIZE_RAW] = { 0, };

  vcd_assert (extent == obj->sectors_written);

  _vcd_make_raw_mode2(buf, data, extent);

  vcd_image_sink_write (obj->image_sink, buf, extent);

  obj->sectors_written++;

  return _callback_wrapper (obj, false);
}

static void
_write_source_mode2_raw (VcdObj *obj, VcdDataSource *source, uint32_t extent)
{
  int n;
  uint32_t sectors;

  sectors = vcd_data_source_stat (source) / M2RAW_SECTOR_SIZE;

  vcd_data_source_seek (source, 0); 

  for (n = 0;n < sectors;n++) {
    char buf[M2RAW_SECTOR_SIZE] = { 0, };

    vcd_data_source_read (source, buf, M2RAW_SECTOR_SIZE, 1);

    if (_write_m2_raw_image_sector (obj, buf, extent+n))
      break;
  }

  vcd_data_source_close (source);
}

static void
_write_source_mode2_form1 (VcdObj *obj, VcdDataSource *source, uint32_t extent)
{
  int n;
  uint32_t sectors, size, last_block_size;

  size = vcd_data_source_stat (source);

  sectors = _vcd_len2blocks (size, CDIO_CD_FRAMESIZE);

  last_block_size = size % CDIO_CD_FRAMESIZE;
  if (!last_block_size)
    last_block_size = CDIO_CD_FRAMESIZE;

  vcd_data_source_seek (source, 0); 

  for (n = 0;n < sectors;n++) {
    char buf[CDIO_CD_FRAMESIZE] = { 0, };

    vcd_data_source_read (source, buf, 
                          ((n + 1 == sectors) 
                           ? last_block_size
                           : CDIO_CD_FRAMESIZE), 1);

    if (_write_m2_image_sector (obj, buf, extent+n, 1, 0, 
                                ((n+1 < sectors) 
                                 ? SM_DATA 
                                 : SM_DATA |SM_EOF),
                                0))
      break;
  }

  vcd_data_source_close (source);
}

static int
_write_sequence (VcdObj *obj, int track_idx)
{
  mpeg_sequence_t *track = 
    _cdio_list_node_data (_vcd_list_at (obj->mpeg_sequence_list, track_idx));
  CdioListNode *pause_node;
  int n, lastsect = obj->sectors_written;
  char buf[2324];
  struct {
    int audio;
    int video;
    int zero;
    int ogt;
    int unknown;
  } mpeg_packets = {0, };


  {
    char *norm_str = NULL;
    const struct vcd_mpeg_stream_vid_info *_info = &track->info->shdr[0];

    switch (vcd_mpeg_get_norm (_info)) {
    case MPEG_NORM_PAL:
      norm_str = strdup ("PAL SIF (352x288/25fps)");
      break;
    case MPEG_NORM_NTSC:
      norm_str = strdup ("NTSC SIF (352x240/29.97fps)");
      break;
    case MPEG_NORM_FILM:
      norm_str = strdup ("FILM SIF (352x240/24fps)");
      break;
    case MPEG_NORM_PAL_S:
      norm_str = strdup ("PAL 2/3 D1 (480x576/25fps)");
      break;
    case MPEG_NORM_NTSC_S:
      norm_str = strdup ("NTSC 2/3 D1 (480x480/29.97fps)");
      break;
	
    case MPEG_NORM_OTHER:
      {
        char buf[1024] = { 0, };
        switch (_info->vsize)
          {
          case 480:
          case 240:
            snprintf (buf, sizeof (buf), "NTSC UNKNOWN (%dx%d/%2.2ffps)",
                      _info->hsize, _info->vsize, _info->frate);
            break;
          case 288:
          case 576:
            snprintf (buf, sizeof (buf), "PAL UNKNOWN (%dx%d/%2.2ffps)",
                      _info->hsize, _info->vsize, _info->frate);
            break;
          default:
            snprintf (buf, sizeof (buf), "UNKNOWN (%dx%d/%2.2ffps)",
                      _info->hsize, _info->vsize, _info->frate);
            break;
          }
        norm_str = strdup (buf);
      }
      break;
    }

    {
      char buf[1024] = { 0, }, buf2[1024] = { 0, };
      int i;

      for (i = 0; i < 3; i++)
        if (track->info->ahdr[i].seen)
          {
            const char *_mode_str[] = {
              0,
              "stereo",
              "jstereo",
              "dual",
              "single",
              0
            };

            snprintf (buf, sizeof (buf), "audio[%d]: l%d/%2.1fkHz/%dkbps/%s ", 
                      i,
                      track->info->ahdr[i].layer,
                      track->info->ahdr[i].sampfreq / 1000.0,
                      track->info->ahdr[i].bitrate / 1024,
                      _mode_str[track->info->ahdr[i].mode]);
                    
            strncat (buf2, buf, sizeof(buf2) - strlen(buf2) - 1);
          }      

      vcd_info ("writing track %d, %s, %s, %s...", track_idx + 2,
                (track->info->version == MPEG_VERS_MPEG1 ? "MPEG1" : "MPEG2"),
                norm_str, buf2);
    }

    free (norm_str);
  }

  for (n = 0; n < obj->track_pregap; n++)
    _write_m2_image_sector (obj, zero, lastsect++, 0, 0, SM_FORM2, 0);

  for (n = 0; n < obj->track_front_margin;n++)
    _write_m2_image_sector (obj, zero, lastsect++, track_idx + 1,
                            0, SM_FORM2|SM_REALT, 0);

  pause_node = _cdio_list_begin (track->pause_list);

  for (n = 0; n < track->info->packets; n++) {
    int ci = 0, sm = 0, cnum = 0, fnum = 0;
    struct vcd_mpeg_packet_info pkt_flags;
    bool set_trigger = false;

    vcd_mpeg_source_get_packet (track->source, n, buf, &pkt_flags, 
                                obj->update_scan_offsets);

    while (pause_node)
      {
        pause_t *_pause = _cdio_list_node_data (pause_node);

        if (!pkt_flags.has_pts)
          break; /* no pts */

        if (pkt_flags.pts < _pause->time)
          break; /* our time has not come yet */

        /* seems it's time to trigger! */
        set_trigger = true;

        vcd_debug ("setting auto pause trigger for time %f (pts %f) @%d", 
                   _pause->time, pkt_flags.pts, n);

        pause_node = _cdio_list_node_next (pause_node);
      }

    switch (vcd_mpeg_packet_get_type (&pkt_flags)) 
      {
    case PKT_TYPE_VIDEO:
      mpeg_packets.video++;
      sm = SM_FORM2|SM_REALT|SM_VIDEO;
      ci = CI_VIDEO;
      cnum = CN_VIDEO;
      break;

    case PKT_TYPE_OGT:
      mpeg_packets.ogt++;
      sm = SM_FORM2|SM_REALT|SM_VIDEO;
      ci = CI_OGT;
      cnum = CN_OGT;
      break;
    
    case PKT_TYPE_AUDIO:
      mpeg_packets.audio++;
      sm = SM_FORM2|SM_REALT|SM_AUDIO;
      ci = CI_AUDIO;
      cnum = CN_AUDIO;
      if (pkt_flags.audio[1] || pkt_flags.audio[2])
        {
          ci = CI_AUDIO2;
          cnum = CN_AUDIO2;
        }
      break;


    case PKT_TYPE_ZERO:
      mpeg_packets.zero++;
      mpeg_packets.unknown--;
    case PKT_TYPE_EMPTY:
      mpeg_packets.unknown++;
      sm = SM_FORM2|SM_REALT;
      ci = CI_EMPTY;
      cnum = CN_EMPTY;
      break;

    case PKT_TYPE_INVALID:
      vcd_error ("invalid mpeg packet found at packet# %d"
                 " -- please fix this mpeg file!", n);
      vcd_mpeg_source_close (track->source);
      return 1;
      break;

    default:
      vcd_assert_not_reached ();
    }

    if (n == track->info->packets - 1)
      {
        sm |= SM_EOR;
        if (!obj->track_rear_margin) /* if no rear margin... */
          sm |= SM_EOF;
      }

    if (set_trigger)
      sm |= SM_TRIG;

    fnum = track_idx + 1;
      
    if (_vcd_obj_has_cap_p (obj, _CAP_4C_SVCD)
        && !obj->svcd_vcd3_mpegav) /* IEC62107 SVCDs have a 
                                      simplified subheader */
      {
        fnum = 1;
        ci = CI_MPEG2;
      }

    if (_write_m2_image_sector (obj, buf, lastsect++, fnum, cnum, sm, ci))
      break;
  }

  vcd_mpeg_source_close (track->source);

  for (n = 0; n < obj->track_rear_margin; n++)
    {
      const uint8_t ci = 0, cnum = 0;
      uint8_t fnum = track_idx + 1;
      uint8_t sm = SM_FORM2 | SM_REALT;

      if (n + 1 == obj->track_rear_margin)
        sm |= SM_EOF;

      _write_m2_image_sector (obj, zero, lastsect++, fnum, cnum, sm, ci);
    }

  vcd_debug ("MPEG packet statistics: %d video, %d audio, %d zero, %d ogt, %d unknown",
             mpeg_packets.video, mpeg_packets.audio, mpeg_packets.zero, mpeg_packets.ogt,
             mpeg_packets.unknown);

  return 0;
}

static int
_write_segment (VcdObj *obj, mpeg_segment_t *_segment)
{
  CdioListNode *pause_node;
  unsigned packet_no;
  int n = obj->sectors_written;

  vcd_assert (_segment->start_extent == n);

  pause_node = _cdio_list_begin (_segment->pause_list);

  for (packet_no = 0;
       packet_no < (_segment->segment_count * VCDINFO_SEGMENT_SECTOR_SIZE);
       packet_no++)
    {
      uint8_t buf[M2F2_SECTOR_SIZE] = { 0, };
      uint8_t fn, cn, sm, ci;

      if (packet_no < _segment->info->packets)
        {
          struct vcd_mpeg_packet_info pkt_flags;
          bool set_trigger = false;
          bool _need_eor = false;

          vcd_mpeg_source_get_packet (_segment->source, packet_no,
                                      buf, &pkt_flags, obj->update_scan_offsets);

          fn = 1;
          cn = CN_EMPTY;
          sm = SM_FORM2 | SM_REALT;
          ci = CI_EMPTY;
  
          while (pause_node)
            {
              pause_t *_pause = _cdio_list_node_data (pause_node);

              if (!pkt_flags.has_pts)
                break; /* no pts */

              if (pkt_flags.pts < _pause->time)
                break; /* our time has not come yet */

              /* seems it's time to trigger! */
              set_trigger = true;

              vcd_debug ("setting auto pause trigger for time %f (pts %f) @%d", 
                         _pause->time, pkt_flags.pts, n);

              pause_node = _cdio_list_node_next (pause_node);
            }
            
          switch (vcd_mpeg_packet_get_type (&pkt_flags)) 
            {
            case PKT_TYPE_VIDEO:
              sm = SM_FORM2 | SM_REALT | SM_VIDEO;

              ci = CI_VIDEO;
              cn = CN_VIDEO;

              if (pkt_flags.video[1])
                ci = CI_STILL, cn = CN_STILL;
              else if (pkt_flags.video[2])
                ci = CI_STILL2, cn = CN_STILL2;

              if (pkt_flags.video[1] || pkt_flags.video[2])
                { /* search for endcode -- hack */
                  int idx;
                
                  for (idx = 0; idx <= 2320; idx++)
                    if (buf[idx] == 0x00
                        && buf[idx + 1] == 0x00
                        && buf[idx + 2] == 0x01
                        && buf[idx + 3] == 0xb7)
                      {
                        _need_eor = true;
                        break;
                      }
                }
              break;

            case PKT_TYPE_AUDIO:
              sm = SM_FORM2 | SM_REALT | SM_AUDIO;
                  
              ci = CI_AUDIO;
              cn = CN_AUDIO;
              break;

            case PKT_TYPE_EMPTY:
              ci = CI_EMPTY;
              cn = CN_EMPTY;
              break;

            default:
              /* fixme -- check.... */
              break;
            }

          if (_vcd_obj_has_cap_p (obj, _CAP_4C_SVCD))
            {
              cn = 1;
              sm = SM_FORM2 | SM_REALT | SM_VIDEO;
              ci = CI_MPEG2;
            }

          if (packet_no + 1 == _segment->info->packets)
            sm |= SM_EOF;

          if (set_trigger)
            sm |= SM_TRIG;

          if (_need_eor)
            {
              vcd_debug ("setting EOR for SeqEnd at packet# %d ('%s')", 
                         packet_no, _segment->id);
              sm |= SM_EOR;
            }
        }
      else
        {
          fn = 1;
          cn = CN_EMPTY;
          sm = SM_FORM2 | SM_REALT;
          ci = CI_EMPTY;

          if (_vcd_obj_has_cap_p (obj, _CAP_4C_SVCD))
            {
              fn = 0;
              sm = SM_FORM2;
            }

        }

      _write_m2_image_sector (obj, buf, n, fn, cn, sm, ci);
          
      n++;
    }

  vcd_mpeg_source_close (_segment->source);

  return 0;
}

static uint32_t
_get_closest_aps (const struct vcd_mpeg_stream_info *_mpeg_info, double t,
                  struct aps_data *_best_aps)
{
  CdioListNode *node;
  struct aps_data best_aps;
  bool first = true;

  vcd_assert (_mpeg_info != NULL);
  vcd_assert (_mpeg_info->shdr[0].aps_list != NULL);

  _CDIO_LIST_FOREACH (node, _mpeg_info->shdr[0].aps_list)
    {
      struct aps_data *_aps = _cdio_list_node_data (node);
  
      if (first)
        {
          best_aps = *_aps;
          first = false;
        }
      else if (fabs (_aps->timestamp - t) < fabs (best_aps.timestamp - t))
        best_aps = *_aps;
      else 
        break;
    }

  if (_best_aps)
    *_best_aps = best_aps;

  return best_aps.packet_no;
}

static void
_update_entry_points (VcdObj *obj)
{
  CdioListNode *sequence_node;

  _CDIO_LIST_FOREACH (sequence_node, obj->mpeg_sequence_list)
    {
      mpeg_sequence_t *_sequence = _cdio_list_node_data (sequence_node);
      CdioListNode *entry_node;
      unsigned last_packet_no = 0;

      _CDIO_LIST_FOREACH (entry_node, _sequence->entry_list)
        {
          entry_t *_entry = _cdio_list_node_data (entry_node);

          _get_closest_aps (_sequence->info, _entry->time, &_entry->aps);

          vcd_log ((fabs (_entry->aps.timestamp - _entry->time) > 1
                    ? VCD_LOG_WARN
                    : VCD_LOG_DEBUG),
                   "requested entry point (id=%s) at %f, "
                   "closest possible entry point at %f",
                   _entry->id, _entry->time, _entry->aps.timestamp);

          if (last_packet_no == _entry->aps.packet_no)
            vcd_warn ("entry point '%s' falls into same sector as previous one!",
                      _entry->id);

          last_packet_no = _entry->aps.packet_no;
        }
    }
}

static int
_write_vcd_iso_track (VcdObj *obj, const time_t *create_time)
{
  CdioListNode *node;
  int n;

  /* generate dir sectors */
  
  _vcd_directory_dump_entries (obj->dir, 
                               _dict_get_bykey (obj, "dir")->buf, 
                               _dict_get_bykey (obj, "dir")->sector);

  _vcd_directory_dump_pathtables (obj->dir, 
                                  _dict_get_bykey (obj, "ptl")->buf, 
                                  _dict_get_bykey (obj, "ptm")->buf);
      
  /* generate PVD and EVD at last... */
  iso9660_set_pvd (_dict_get_bykey (obj, "pvd")->buf,
                   obj->iso_volume_label, 
                   obj->iso_publisher_id,
                   obj->iso_preparer_id,
                   obj->iso_application_id, 
                   obj->iso_size, 
                   _dict_get_bykey (obj, "dir")->buf, 
                   _dict_get_bykey (obj, "ptl")->sector,
                   _dict_get_bykey (obj, "ptm")->sector,
                   iso9660_pathtable_get_size (_dict_get_bykey (obj, "ptm")->buf),
                   create_time
);
    
  iso9660_set_evd (_dict_get_bykey (obj, "evd")->buf);

  /* fill VCD relevant files with data */

  set_info_vcd (obj, _dict_get_bykey (obj, "info")->buf);
  set_entries_vcd (obj, _dict_get_bykey (obj, "entries")->buf);

  if (_vcd_pbc_available (obj))
    {
      if (_vcd_obj_has_cap_p (obj, _CAP_PBC_X))
        {
          set_lot_vcd (obj, _dict_get_bykey (obj, "lot_x")->buf, true);
          set_psd_vcd (obj, _dict_get_bykey (obj, "psd_x")->buf, true);
        }

      _vcd_pbc_check_unreferenced (obj);
  
      set_lot_vcd (obj, _dict_get_bykey (obj, "lot")->buf, false);
      set_psd_vcd (obj, _dict_get_bykey (obj, "psd")->buf, false);
    }

  if (_vcd_obj_has_cap_p (obj, _CAP_4C_SVCD))
    {
      set_tracks_svd (obj, _dict_get_bykey (obj, "tracks")->buf);
      set_search_dat (obj, _dict_get_bykey (obj, "search")->buf);
      set_scandata_dat (obj, _dict_get_bykey (obj, "scandata")->buf);
    }

  /* start actually writing stuff */

  vcd_info ("writing track 1 (ISO9660)...");

  /* 00:02:00 -> 00:04:74 */
  for (n = 0;n < obj->mpeg_segment_start_extent; n++)
    {
      const void *content = NULL;
      uint8_t flags = SM_DATA;

      content = _dict_get_sector (obj, n);
      flags |= _dict_get_sector_flags (obj, n);
      
      if (content == NULL)
        content = zero;

      _write_m2_image_sector (obj, content, n, 0, 0, flags, 0);
    }

  /* SEGMENTS */

  vcd_assert (n == obj->mpeg_segment_start_extent);

  _CDIO_LIST_FOREACH (node, obj->mpeg_segment_list)
    {
      mpeg_segment_t *_segment = _cdio_list_node_data (node);

      _write_segment (obj, _segment);
    }

  n = obj->sectors_written;

  /* EXT stuff */

  vcd_assert (n == obj->ext_file_start_extent);

  for (;n < obj->custom_file_start_extent; n++)
    {
      const void *content = NULL;
      uint8_t flags = SM_DATA;
      uint8_t fileno = _vcd_obj_has_cap_p (obj, _CAP_4C_SVCD) ? 0 : 1;

      content = _dict_get_sector (obj, n);
      flags |= _dict_get_sector_flags (obj, n);
      
      if (content == NULL)
        {
          vcd_debug ("unexpected empty EXT sector");
          content = zero;
        }
      
      _write_m2_image_sector (obj, content, n, fileno, 0, flags, 0);
    }

  /* write custom files */

  vcd_assert (n == obj->custom_file_start_extent);
    
  _CDIO_LIST_FOREACH (node, obj->custom_file_list)
    {
      custom_file_t *p = _cdio_list_node_data (node);
        
      vcd_info ("writing file `%s' (%lu bytes%s)", 
                p->iso_pathname, (unsigned long) p->size, 
                p->raw_flag ? ", raw sectors file": "");
      if (p->raw_flag)
        _write_source_mode2_raw (obj, p->file, p->start_extent);
      else
        _write_source_mode2_form1 (obj, p->file, p->start_extent);
    }
  
  /* blank unalloced tracks */
  while ((n = _vcd_salloc (obj->iso_bitmap, SECTOR_NIL, 1)) < obj->iso_size)
    _write_m2_image_sector (obj, zero, n, 0, 0, SM_DATA, 0);

  return 0;
}


long
vcd_obj_get_image_size (VcdObj *obj)
{
  long size_sectors = -1;

  vcd_assert (!obj->in_output);
  
  if (_cdio_list_length (obj->mpeg_sequence_list) > 0) 
    {
      /* fixme -- make this efficient */
      size_sectors = vcd_obj_begin_output (obj);
      vcd_obj_end_output (obj);
    }

  return size_sectors;
}

long
vcd_obj_begin_output (VcdObj *obj)
{
  uint32_t image_size;

  vcd_assert (obj != NULL);
  vcd_assert (_cdio_list_length (obj->mpeg_sequence_list) > 0);

  vcd_assert (!obj->in_output);
  obj->in_output = true;

  obj->in_track = 1;
  obj->sectors_written = 0;

  obj->iso_bitmap = _vcd_salloc_new ();

  obj->dir = _vcd_directory_new ();

  obj->buffer_dict_list = _cdio_list_new ();

  _finalize_vcd_iso_track (obj);

  _update_entry_points (obj);

  image_size = obj->relative_end_extent + obj->iso_size;

  image_size += obj->leadout_pregap;

  if (image_size > CDIO_CD_MAX_SECTORS)
    vcd_error ("image too big (%d sectors > %d sectors)", 
               (unsigned) image_size, (unsigned) CDIO_CD_MAX_SECTORS);

  {
    char *_tmp = cdio_lba_to_msf_str (image_size);

    if (image_size > CDIO_CD_74MIN_SECTORS)
      vcd_warn ("generated image (%d sectors [%s]) may not fit "
                "on 74min CDRs (%d sectors)", 
                (unsigned) image_size, _tmp, (unsigned) CDIO_CD_74MIN_SECTORS);

    free (_tmp);
  }

  return image_size;
}


void
vcd_obj_end_output (VcdObj *obj)
{
  vcd_assert (obj != NULL);

  vcd_assert (obj->in_output);
  obj->in_output = false;

  _vcd_directory_destroy (obj->dir);
  _vcd_salloc_destroy (obj->iso_bitmap);

  _dict_clean (obj);
  _cdio_list_free (obj->buffer_dict_list, true);
}

int
vcd_obj_append_pbc_node (VcdObj *obj, struct _pbc_t *_pbc)
{
  vcd_assert (obj != NULL);
  vcd_assert (_pbc != NULL);

  if (!_vcd_obj_has_cap_p (obj, _CAP_PBC))
    {
      vcd_error ("PBC not supported for current VCD type");
      return -1;
    }

  if (_pbc->item_id && _vcd_pbc_lookup (obj, _pbc->item_id))
    {
      vcd_error ("item id (%s) exists already", _pbc->item_id);
      return -1;
    }
  
  _cdio_list_append (obj->pbc_list, _pbc);

  return 0;
}

int
vcd_obj_write_image (VcdObj *obj, VcdImageSink *image_sink,
                     progress_callback_t callback, void *user_data,
                     const time_t *create_time)
{
  CdioListNode *node;

  vcd_assert (obj != NULL);
  vcd_assert (obj->in_output);

  if (!image_sink)
    return -1;

  /* start with meta info */

  {
    CdioList *cue_list;
    vcd_cue_t *_cue;

    cue_list = _cdio_list_new ();

    _cdio_list_append (cue_list, (_cue = _vcd_malloc (sizeof (vcd_cue_t))));

    _cue->lsn = 0;
    _cue->type = VCD_CUE_TRACK_START;

    _CDIO_LIST_FOREACH (node, obj->mpeg_sequence_list)
      {
        mpeg_sequence_t *track = _cdio_list_node_data (node);
        CdioListNode *entry_node;

        _cdio_list_append (cue_list, 
                           (_cue = _vcd_malloc (sizeof (vcd_cue_t))));
        
        _cue->lsn = track->relative_start_extent + obj->iso_size;
        _cue->lsn -= obj->track_pregap;
        _cue->type = VCD_CUE_PREGAP_START;

        _cdio_list_append (cue_list, 
                           (_cue = _vcd_malloc (sizeof (vcd_cue_t))));

        _cue->lsn = track->relative_start_extent + obj->iso_size;
        _cue->type = VCD_CUE_TRACK_START;

        _CDIO_LIST_FOREACH (entry_node, track->entry_list)
          {
            entry_t *_entry = _cdio_list_node_data (entry_node);

            _cdio_list_append (cue_list, 
                               (_cue = _vcd_malloc (sizeof (vcd_cue_t))));
            
            _cue->lsn = obj->iso_size;
            _cue->lsn += track->relative_start_extent;
            _cue->lsn += obj->track_front_margin;
            _cue->lsn += _entry->aps.packet_no;

            _cue->type = VCD_CUE_SUBINDEX;
          }
      }

    /* add last one... */

    _cdio_list_append (cue_list, (_cue = _vcd_malloc (sizeof (vcd_cue_t))));

    _cue->lsn = obj->relative_end_extent + obj->iso_size;

    _cue->lsn += obj->leadout_pregap;

    _cue->type = VCD_CUE_END;

    /* send it to image object */

    vcd_image_sink_set_cuesheet (image_sink, cue_list);

    _cdio_list_free (cue_list, true);
  }

  /* and now for the pay load */

  {
    unsigned track;

    vcd_assert (obj != NULL);
    vcd_assert (obj->sectors_written == 0);

    vcd_assert (obj->in_output);

    obj->progress_callback = callback;
    obj->callback_user_data = user_data;
    obj->image_sink = image_sink;
  
    if (_callback_wrapper (obj, true))
      return 1;

    if (_write_vcd_iso_track (obj, create_time))
      return 1;

    if (obj->update_scan_offsets)
      vcd_info ("'update scan offsets' option enabled for the following tracks!");

    for (track = 0;track < _cdio_list_length (obj->mpeg_sequence_list);track++)
      {
        obj->in_track++;

        if (_callback_wrapper (obj, true))
          return 1;

        if (_write_sequence (obj, track))
          return 1;
      }

    if (obj->leadout_pregap)
      {
        int n, lastsect = obj->sectors_written;

        vcd_debug ("writting post-gap ('leadout pregap')...");
        
        for (n = 0; n < obj->leadout_pregap; n++)
          _write_m2_image_sector (obj, zero, lastsect++, 0, 0, SM_FORM2, 0);
      }

    if (_callback_wrapper (obj, true))
      return 1;

    obj->image_sink = NULL;
  
    vcd_image_sink_destroy (image_sink);

    return 0; /* ok */
  }
}  

const char *
vcd_version_string (bool full_text)
{
  if (!full_text)
    return ("GNU VCDImager " VERSION " [" HOST_ARCH "]");

  return ("%s (GNU VCDImager) " VERSION "\n"
          "Written by Herbert Valerio Riedel and Rocky Bernstein.\n"
          "\n"
          "http://www.gnu.org/software/vcdimager/\n"
          "\n"
          "Copyright (C) 2000-2003 Herbert Valerio Riedel <hvr@gnu.org>\n"
	  "                   2003 Rocky Bernstein <rocky@panix.com>\n"
          "\n"
          "This is free software; see the source for copying conditions.  There is NO\n"
          "warranty; not even for MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.\n");
}


/* 
 * Local variables:
 *  c-file-style: "gnu"
 *  tab-width: 8
 *  indent-tabs-mode: nil
 * End:
 */
