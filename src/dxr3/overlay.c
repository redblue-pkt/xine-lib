/* 
 * Copyright (C) 2000-2001 the xine project
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
 * $Id: overlay.c,v 1.4 2001/10/24 18:39:31 mlampard Exp $
 *
 * Overlay support routines for video_out_dxr3
 */


#include <sys/types.h>
#include <sys/ioctl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include <linux/em8300.h>
#include "dxr3_overlay.h"

#define TYPE_INT 1
#define TYPE_XINT 2
#define TYPE_COEFF 3
#define TYPE_FLOAT 4

struct lut_entry {
    char *name;   
    int type;     
    void *ptr;    
};

static struct lut_entry *new_lookuptable(dxr3_overlay_t *this)
{
	struct lut_entry m[] = {
		{"xoffset", TYPE_INT, &this->xoffset},
		{"yoffset", TYPE_INT, &this->yoffset},
		{"xcorr", TYPE_INT, &this->xcorr},
		{"jitter", TYPE_INT, &this->jitter},
		{"stability", TYPE_INT, &this->stability},
		{"keycolor", TYPE_XINT, &this->colorkey},
		{"colcal_upper", TYPE_COEFF, &this->colcal_upper[0]},
		{"colcal_lower", TYPE_COEFF, &this->colcal_lower[0]},
		{"color_interval", TYPE_FLOAT, &this->color_interval},
		{0,0,0}
	},*p;

	p = malloc(sizeof(m));
	memcpy(p,m,sizeof(m));
	return p;
}

static int lookup_parameter(struct lut_entry *lut, char *name,
 void **ptr, int *type) 
{
	int i;

	for(i=0; lut[i].name; i++)
	 if(!strcmp(name,lut[i].name)) {
		*ptr = lut[i].ptr;
		*type = lut[i].type;
		return 1;
	 }
	return 0;
}

int dxr3_overlay_read_state(dxr3_overlay_t *this)
{
	char *tok;
	char fname[128],tmp[128],line[256];
	FILE *fp;
	struct lut_entry *lut;
	void *ptr;
	int type;
	int j;

	strcpy(fname,getenv("HOME"));
	strcat(fname,"/.overlay");	    

	sprintf(tmp,"/res_%dx%dx%d",
	 this->screen_xres,this->screen_yres,this->screen_depth);
	strcat(fname,tmp);

	if(!(fp=fopen(fname,"r"))){
		printf("ERRROR Reading overlay init file!! run autocal !!!\n");
	return -1;
	}

	lut = new_lookuptable(this);

	while(!feof(fp)) {
		if(!fgets(line,256,fp))
			break;
		tok=strtok(line," ");
		if(lookup_parameter(lut,tok,&ptr,&type)) {
			tok=strtok(NULL," ");
			switch(type) {
			case TYPE_INT:
				sscanf(tok,"%d",(int *)ptr);
				break;
			case TYPE_XINT:
				sscanf(tok,"%x",(int *)ptr);
				break;
			case TYPE_FLOAT:
				sscanf(tok,"%f",(float *)ptr);
				break;
			case TYPE_COEFF:
				for(j=0;j<3;j++) {
					sscanf(tok,"%f",&((struct coeff *)ptr)[j].k);
					tok=strtok(NULL," ");
					sscanf(tok,"%f",&((struct coeff *)ptr)[j].m);
					tok=strtok(NULL," ");
				}
				break;	    
			}
		}	
	}
	free(lut);
	fclose(fp);
	return 0;
}


static int col_interp(float x, struct coeff c)
{
	int y;
	y = rint(x*c.k + c.m);
	if (y > 255) y = 255;
	if (y <   0) y =   0;
	return y;
}

int dxr3_overlay_set_keycolor(dxr3_overlay_t *this)
{
	float r = (this->colorkey & 0xff0000) >> 16;
	float g = (this->colorkey & 0x00ff00) >>  8;
	float b = (this->colorkey & 0x0000ff);
	float interval = this->color_interval;
	int ret;
	int32_t overlay_limit;
	em8300_attribute_t attr;

	overlay_limit =  /* lower limit */
		col_interp(r - interval, this->colcal_upper[0]) << 16 |
		col_interp(g - interval, this->colcal_upper[1]) <<  8 |
		col_interp(b - interval, this->colcal_upper[2]);

	attr.attribute = EM9010_ATTRIBUTE_KEYCOLOR_LOWER;
	attr.value = overlay_limit;
	ret = ioctl(this->fd_control, EM8300_IOCTL_OVERLAY_SET_ATTRIBUTE, &attr);
	if (ret < 0) return ret;

	overlay_limit =  /* upper limit */
		col_interp(r + interval, this->colcal_upper[0]) << 16 |
		col_interp(g + interval, this->colcal_upper[1]) <<  8 |
		col_interp(b + interval, this->colcal_upper[2]);

	attr.attribute = EM9010_ATTRIBUTE_KEYCOLOR_UPPER;
	attr.value = overlay_limit;
	return ioctl(this->fd_control, EM8300_IOCTL_OVERLAY_SET_ATTRIBUTE, &attr);
}


int dxr3_overlay_set_mode(dxr3_overlay_t *this, int mode)
{
	return ioctl(this->fd_control, EM8300_IOCTL_OVERLAY_SETMODE, &mode);
}

int dxr3_overlay_set_attributes(dxr3_overlay_t *this)
{
	em8300_attribute_t attr;
	attr.attribute = EM9010_ATTRIBUTE_XOFFSET;
	attr.value = this->xoffset;
	if(ioctl(this->fd_control, EM8300_IOCTL_OVERLAY_SET_ATTRIBUTE, &attr) == -1)
		return -1;
	attr.attribute = EM9010_ATTRIBUTE_YOFFSET;
	attr.value = this->yoffset;
	if(ioctl(this->fd_control, EM8300_IOCTL_OVERLAY_SET_ATTRIBUTE, &attr) == -1)
		return -1;
	attr.attribute = EM9010_ATTRIBUTE_XCORR;
	attr.value = this->xcorr;
	if(ioctl(this->fd_control, EM8300_IOCTL_OVERLAY_SET_ATTRIBUTE, &attr) == -1)
		return -1;
	attr.attribute = EM9010_ATTRIBUTE_STABILITY;
	attr.value = this->stability;
	if(ioctl(this->fd_control, EM8300_IOCTL_OVERLAY_SET_ATTRIBUTE, &attr) == -1)
		return -1;
	attr.attribute = EM9010_ATTRIBUTE_JITTER;
	attr.value = this->jitter;
	return ioctl(this->fd_control, EM8300_IOCTL_OVERLAY_SET_ATTRIBUTE, &attr);
}

int dxr3_overlay_set_screen(dxr3_overlay_t *this)
{
	em8300_overlay_screen_t scr;
	scr.xsize = this->screen_xres;
	scr.ysize = this->screen_yres;
	return ioctl(this->fd_control, EM8300_IOCTL_OVERLAY_SETSCREEN, &scr);
}

int dxr3_overlay_set_window(dxr3_overlay_t *this,
 int xpos, int ypos, int width, int height)
{
	em8300_overlay_window_t win;

	/* is some part of the picture visible? */
	if (xpos+width  < 0) return 0;
	if (ypos+height < 0) return 0;
	if (xpos > this->screen_xres) return 0;
	if (ypos > this->screen_yres) return 0;
	
	win.xpos = xpos;
	win.ypos = ypos;
	win.width = width;
	win.height = height;
	return ioctl(this->fd_control, EM8300_IOCTL_OVERLAY_SETWINDOW, &win);
}

int dxr3_overlay_set_signalmode(dxr3_overlay_t *this,int mode)
{
	return ioctl(this->fd_control, EM8300_IOCTL_OVERLAY_SIGNALMODE, &mode);
}

void dxr3_overlay_buggy_preinit(dxr3_overlay_t *this, int fd)
{
	int mode = 0; 

	/* TODO: catch errors */
	this->fd_control = fd;
	dxr3_overlay_set_screen(this);
	dxr3_overlay_set_window(this, 1,1, 2,2);
	dxr3_overlay_set_keycolor(this);
 	dxr3_overlay_set_attributes(this);
	dxr3_overlay_set_mode(this, EM8300_OVERLAY_MODE_OVERLAY);
}
