/*
 * Copyright (C) 2004 the xine project
 * 
 * This file is part of xine, a free video player.
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
 * $Id: input_smb.c,v 1.4 2005/02/06 15:00:36 tmattern Exp $
 */


#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "xine_internal.h"
#include "xineutils.h"
#include "compat.h"
#include "input_plugin.h"

#include <libsmbclient.h>
#include <sys/types.h>
#include <errno.h>

typedef struct {
	input_class_t input_class;
	xine_t *xine;
} smb_input_class_t;

typedef struct {
	input_plugin_t input_plugin;
	xine_stream_t *stream;

	/* File */
	char *mrl;
	int fd;
} smb_input_t;


static uint32_t
smb_plugin_get_capabilities (input_plugin_t *this_gen)
{
	return INPUT_CAP_SEEKABLE; // | INPUT_CAP_SPULANG;
}


static off_t
smb_plugin_read (input_plugin_t *this_gen, char *buf, off_t len)
{
	smb_input_t *this = (smb_input_t *) this_gen;
	off_t n, num_bytes;

	num_bytes = 0;

	while (num_bytes < len)
	{
		n = smbc_read( this->fd, buf+num_bytes, len-num_bytes );
		if (n<1) return -1;
		if (!n) return num_bytes;
		num_bytes += n;
	}

	return num_bytes;
}

static buf_element_t*
smb_plugin_read_block (input_plugin_t *this_gen, fifo_buffer_t *fifo,
		off_t todo)
{
	off_t total_bytes;
	buf_element_t *buf = fifo->buffer_pool_alloc (fifo);

	buf->content = buf->mem;
	buf->type = BUF_DEMUX_BLOCK;

	total_bytes = smb_plugin_read (this_gen, buf->content, todo);

	if (total_bytes == todo) buf->size = todo;
	else
	{
		buf->free_buffer (buf);
		buf = NULL;
	}

	return buf;
}

static off_t
smb_plugin_seek (input_plugin_t *this_gen, off_t offset, int origin)
{
	smb_input_t *this = (smb_input_t *) this_gen;

	if (this->fd<0) return 0;
	return smbc_lseek(this->fd,offset,origin);
}

static off_t
smb_plugin_get_current_pos (input_plugin_t *this_gen)
{
	smb_input_t *this = (smb_input_t *) this_gen;

	if (this->fd<0) return 0;
	return smbc_lseek(this->fd,0,SEEK_CUR);
}

static off_t
smb_plugin_get_length (input_plugin_t *this_gen)
{
	smb_input_t *this = (smb_input_t *) this_gen;

	int e;
	struct stat st;
	
	if (this->fd>=0) e = smbc_fstat(this->fd,&st);
	else e = smbc_stat(this->mrl,&st);

	if (e) return 0;

	return st.st_size;
}

static char*
smb_plugin_get_mrl (input_plugin_t *this_gen)
{
	smb_input_t *this = (smb_input_t *) this_gen;

	return this->mrl;
}

static char
*smb_class_get_description (input_class_t *this_gen)
{
	return _("CIFS/SMB input plugin based on libsmbclient");
}

static const char
*smb_class_get_identifier (input_class_t *this_gen)
{
	return "smb";
}

static int
smb_plugin_get_optional_data (input_plugin_t *this_gen, 
		void *data, int data_type)
{
	return INPUT_OPTIONAL_UNSUPPORTED;
}

static void
smb_plugin_dispose (input_plugin_t *this_gen )
{
	smb_input_t *this = (smb_input_t *) this_gen;

	if (this->fd>=0)
		smbc_close(this->fd);
	if (this->mrl)
		free (this->mrl);
	free (this);
}

static int
smb_plugin_open (input_plugin_t *this_gen )
{
	smb_input_t *this = (smb_input_t *) this_gen;
	smb_input_class_t *class = (smb_input_class_t *) this_gen->input_class;

	this->fd = smbc_open(this->mrl,O_RDONLY,0);
	xprintf(class->xine, XINE_VERBOSITY_DEBUG, 
	        "input_smb: open failed for %s: %s\n", 
	        this->mrl, strerror(errno));
	if (this->fd<0) return 0;

	return 1;
}

static void
smb_class_dispose (input_class_t *this_gen)
{
	smb_input_class_t *this = (smb_input_class_t *) this_gen;

	free (this);
}

static input_plugin_t *
smb_class_get_instance (input_class_t *class_gen, xine_stream_t *stream,
		const char *mrl)
{
	smb_input_t *this;
	
	if (mrl == NULL)
		return NULL;
	if (strncmp (mrl, "smb://",6))
		return NULL;

	this = (smb_input_t *)xine_xmalloc(sizeof(smb_input_t));
	this->stream = stream;
	this->mrl = strdup (mrl);
	this->fd = -1;

	this->input_plugin.open              = smb_plugin_open;
	this->input_plugin.get_capabilities  = smb_plugin_get_capabilities;
	this->input_plugin.read              = smb_plugin_read;
	this->input_plugin.read_block        = smb_plugin_read_block;
	this->input_plugin.seek              = smb_plugin_seek;
	this->input_plugin.get_current_pos   = smb_plugin_get_current_pos;
	this->input_plugin.get_length        = smb_plugin_get_length;
	this->input_plugin.get_blocksize     = NULL;
	this->input_plugin.get_mrl           = smb_plugin_get_mrl;
	this->input_plugin.get_optional_data =
		smb_plugin_get_optional_data;
	this->input_plugin.dispose           = smb_plugin_dispose;
	this->input_plugin.input_class       = class_gen;

	return &this->input_plugin;
}

void smb_auth(const char *srv, const char *shr, char *wg, int wglen, char *un, int unlen, char *pw, int pwlen)
{
	wglen = unlen = pwlen = 0;
}

static void
*init_input_class (xine_t *xine, void *data)
{
	smb_input_class_t *this;

	if (smbc_init(smb_auth,(xine->verbosity >= XINE_VERBOSITY_DEBUG)))
	  return NULL;

	this = (smb_input_class_t *) xine_xmalloc(sizeof(smb_input_class_t));
	this->xine = xine;

	this->input_class.get_instance       = smb_class_get_instance;
	this->input_class.get_identifier     = smb_class_get_identifier;
	this->input_class.get_description    = smb_class_get_description;
	this->input_class.get_dir            = NULL;
	this->input_class.get_autoplay_list  = NULL;
	this->input_class.dispose            = smb_class_dispose;
	this->input_class.eject_media        = NULL;

	return (input_class_t *) this;
}

static input_info_t input_info_smb = {
  0                       /* priority */
};

plugin_info_t xine_plugin_info[] = {
	{ PLUGIN_INPUT, 16, "smb", XINE_VERSION_CODE, &input_info_smb,
		init_input_class },
	{ PLUGIN_NONE, 0, "", 0, NULL, NULL }
};

