/*
 * Copyright (C) 2000-2003 the xine project
 * 2002 Bastien Nocera <hadess@hadess.net>
 * 
 * This file is part of totem,
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
 * $Id: input_gnome_vfs.c,v 1.10 2003/06/08 21:58:09 hadess Exp $
 */


#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "xine_internal.h"
#include "xineutils.h"
#include "compat.h"
#include "input_plugin.h"

#include <libgnomevfs/gnome-vfs.h>

#define D(x...)
/* #define D(x...) g_message (x) */
/* #define LOG */

typedef struct {
	input_class_t input_class;
	xine_t *xine;
} gnomevfs_input_class_t;

typedef struct {
	input_plugin_t input_plugin;
	xine_stream_t *stream;

	/* File */
	GnomeVFSHandle *fh;
	off_t curpos;
	char *mrl;
	GnomeVFSURI *uri;

	/* Preview */
	char preview[MAX_PREVIEW_SIZE];
	off_t preview_size;
	off_t preview_pos;
} gnomevfs_input_t;

static off_t gnomevfs_plugin_get_current_pos (input_plugin_t *this_gen);


static uint32_t
gnomevfs_plugin_get_capabilities (input_plugin_t *this_gen)
{
	return INPUT_CAP_SEEKABLE | INPUT_CAP_SPULANG;
}

static off_t
gnomevfs_plugin_read (input_plugin_t *this_gen, char *buf, off_t len)
{
	gnomevfs_input_t *this = (gnomevfs_input_t *) this_gen;
	off_t n, num_bytes;

	D("gnomevfs_plugin_read: %ld", (long int) len);

	num_bytes = 0;

	while (num_bytes < len)
	{
		GnomeVFSResult res;

		res = gnome_vfs_read (this->fh, &buf[num_bytes],
				(GnomeVFSFileSize) (len - num_bytes),
				(GnomeVFSFileSize *)&n);

		D("gnomevfs_plugin_read: read %ld from gnome-vfs",
				(long int) n);
		if (res != GNOME_VFS_OK && res != GNOME_VFS_ERROR_EOF)
		{
			D("gnomevfs_plugin_read: gnome_vfs_read returns %s",
					gnome_vfs_result_to_string (res));
			return -1;
		} else if (res == GNOME_VFS_ERROR_EOF) {
			D("gnomevfs_plugin_read: GNOME_VFS_ERROR_EOF");
			return 0;
		}

		if (n <= 0)
		{
			g_warning ("input_gnomevfs: read error");
		}

		num_bytes += n;
		this->curpos += n;
	}

	return num_bytes;
}

static buf_element_t*
gnomevfs_plugin_read_block (input_plugin_t *this_gen, fifo_buffer_t *fifo,
		off_t todo)
{
	off_t total_bytes;
	buf_element_t *buf = fifo->buffer_pool_alloc (fifo);

	buf->content = buf->mem;
	buf->type = BUF_DEMUX_BLOCK;

	total_bytes = gnomevfs_plugin_read (this_gen, buf->content, todo);

	while (total_bytes != todo)
	{
		buf->free_buffer (buf);
		buf = NULL;
	}

	if (buf != NULL)
		buf->size = total_bytes;

	return buf;
}

static off_t
gnomevfs_plugin_seek (input_plugin_t *this_gen, off_t offset, int origin)
{
	gnomevfs_input_t *this = (gnomevfs_input_t *) this_gen;

	if (gnome_vfs_seek (this->fh, origin, offset) == GNOME_VFS_OK)
	{
		D ("gnomevfs_plugin_seek: %d", (int) (origin + offset));
		return (off_t) (origin + offset);
	} else
		return (off_t) gnomevfs_plugin_get_current_pos (this_gen);
}

static off_t
gnomevfs_plugin_get_current_pos (input_plugin_t *this_gen)
{
	gnomevfs_input_t *this = (gnomevfs_input_t *) this_gen;
	GnomeVFSFileSize offset;

	if (this->fh == NULL)
	{
		D ("gnomevfs_plugin_get_current_pos: (this->fh == NULL)");
		return 0;
	}

	if (gnome_vfs_tell (this->fh, &offset) == GNOME_VFS_OK)
	{
		D ("gnomevfs_plugin_get_current_pos: %d", (int) offset);
		return (off_t) offset;
	} else
		return 0;
}

static off_t
gnomevfs_plugin_get_length (input_plugin_t *this_gen)
{
	gnomevfs_input_t *this = (gnomevfs_input_t *) this_gen;
	GnomeVFSFileInfo info;

	if (this->fh == NULL)
	{
		D ("gnomevfs_plugin_get_length: (this->fh == NULL)");
		return 0;
	}

	if (gnome_vfs_get_file_info (this->mrl,
				&info,
				GNOME_VFS_FILE_INFO_DEFAULT) == GNOME_VFS_OK)
	{
		D ("gnomevfs_plugin_get_length: %d", (int) info.size);
		return (off_t) info.size;
	} else
		return 0;
}

static uint32_t
gnomevfs_plugin_get_blocksize (input_plugin_t *this_gen)
{
	return 0;
}

static int
gnomevfs_klass_eject_media (input_class_t *this_gen)
{
	return 1; /* doesn't make sense */
}

static char*
gnomevfs_plugin_get_mrl (input_plugin_t *this_gen)
{
	gnomevfs_input_t *this = (gnomevfs_input_t *) this_gen;

	return this->mrl;
}

static char
*gnomevfs_klass_get_description (input_class_t *this_gen)
{
	return _("gnome-vfs input plugin as shipped with xine");
}

static char
*gnomevfs_klass_get_identifier (input_class_t *this_gen)
{
	return "gnomevfs";
}

static int
gnomevfs_plugin_get_optional_data (input_plugin_t *this_gen, 
		void *data, int data_type)
{
	D ("input_gnomevfs: get optional data, type %08x\n", data_type);

	return INPUT_OPTIONAL_UNSUPPORTED;
}

static void
gnomevfs_plugin_dispose (input_plugin_t *this_gen )
{
	gnomevfs_input_t *this = (gnomevfs_input_t *) this_gen;

	if (this->fh)
		gnome_vfs_close (this->fh);
	if (this->mrl)
		g_free (this->mrl);
	if (this->uri)
		gnome_vfs_uri_unref (this->uri);
	g_free (this);
}

static int
gnomevfs_plugin_open (input_plugin_t *this_gen )
{
	gnomevfs_input_t *this = (gnomevfs_input_t *) this_gen;

	D("gnomevfs_klass_open: opening '%s'", this->mrl);
	if (gnome_vfs_open_uri (&this->fh, this->uri, GNOME_VFS_OPEN_READ) != GNOME_VFS_OK)
	{
		D("gnomevfs_klass_open: failed to open '%s'", this->mrl);
		return 0;
	}

	return 1;

}

static void
gnomevfs_klass_dispose (input_class_t *this_gen)
{
	gnomevfs_input_class_t *this = (gnomevfs_input_class_t *) this_gen;

	g_free (this);
}


static input_plugin_t *
gnomevfs_klass_get_instance (input_class_t *klass_gen, xine_stream_t *stream,
		const char *mrl)
{
	gnomevfs_input_t *this;
	GnomeVFSURI *uri;

	D("gnomevfs_klass_get_instance: %s", mrl);

	uri = gnome_vfs_uri_new (mrl);
	if (uri == NULL)
		return NULL;

	/* local files should be handled by the file input */
	if (strncmp (mrl, "file:/", strlen ("file:/")) == 0)
	{
		D("gnomevfs_klass_open: '%s' is a file:///", mrl);
		gnome_vfs_uri_unref (uri);
		return NULL;
	} else if (strncmp (gnome_vfs_uri_get_scheme (uri), "http", 4) == 0) {
		D("gnomevfs_klass_open: '%s' is http://", mrl);
		gnome_vfs_uri_unref (uri);
		return NULL;
	}

	D("Creating the structure for stream '%s'", mrl);
	this = g_new0 (gnomevfs_input_t, 1);
	this->stream = stream;
	this->fh = NULL;
	this->mrl = g_strdup (mrl);
	this->uri = uri;

	this->input_plugin.open              = gnomevfs_plugin_open;
	this->input_plugin.get_capabilities  = gnomevfs_plugin_get_capabilities;
	this->input_plugin.read              = gnomevfs_plugin_read;
	this->input_plugin.read_block        = gnomevfs_plugin_read_block;
	this->input_plugin.seek              = gnomevfs_plugin_seek;
	this->input_plugin.get_current_pos   = gnomevfs_plugin_get_current_pos;
	this->input_plugin.get_length        = gnomevfs_plugin_get_length;
	this->input_plugin.get_blocksize     = gnomevfs_plugin_get_blocksize;
	this->input_plugin.get_mrl           = gnomevfs_plugin_get_mrl;
	this->input_plugin.get_optional_data =
		gnomevfs_plugin_get_optional_data;
	this->input_plugin.dispose           = gnomevfs_plugin_dispose;
	this->input_plugin.input_class       = klass_gen;

	return &this->input_plugin;
}

static void
*init_input_class (xine_t *xine, void *data)
{
	gnomevfs_input_class_t *this;

	D("init_input_class");

	if (gnome_vfs_initialized () == FALSE)
		if (gnome_vfs_init () == FALSE)
			return NULL;

	if (!g_thread_supported ())
		g_thread_init (NULL);

	this = g_new0 (gnomevfs_input_class_t, 1);
	this->xine = xine;

	this->input_class.get_instance       = gnomevfs_klass_get_instance;
	this->input_class.get_identifier     = gnomevfs_klass_get_identifier;
	this->input_class.get_description    = gnomevfs_klass_get_description;
	this->input_class.get_dir            = NULL;
	this->input_class.get_autoplay_list  = NULL;
	this->input_class.dispose            = gnomevfs_klass_dispose;
	this->input_class.eject_media        = gnomevfs_klass_eject_media;

	return (input_class_t *) this;
}

plugin_info_t xine_plugin_info[] = {
	{ PLUGIN_INPUT, 13, "gnomevfs", XINE_VERSION_CODE, NULL,
		init_input_class },
	{ PLUGIN_NONE, 0, "", 0, NULL, NULL }
};

