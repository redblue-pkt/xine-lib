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
 * $Id: dxr3_vo_core.c,v 1.11 2001/11/29 07:17:08 mlampard Exp $
 *
 *************************************************************************
 * core functions common to both Standard and RT-Encoding vo plugins     *
 *									 *
 * functions in this file (in order of appearance):			 *			 *
 *	malloc_aligned							 *
 *	dxr3_overlay_adapt_area						 *
 *	dxr3_get_keycolor						 *
 *	dxr3_read_config						 *
 *	is_fullscreen							 *
 *	dxr3_zoomTV							 *
 *	dxr3_get_property						 *	
 *	dxr3_set_property						 *
 *	dxr3_get_property_min_max					 *
 *	dxr3_translate_gui2video					 *
 *	dxr3_gui_data_exchange						 *
 *	dxr3_gather_screen_vars						 *
 *									 *
 *	and overlay specific functions formerly in overlay.c		 *
 *************************************************************************/
 
#include "dxr3_video_out.h"

void *malloc_aligned (size_t alignment, size_t size, void **mem) {
  char *aligned;
   
  aligned = malloc (size+alignment);
  *mem = aligned;
        
  while ((int) aligned % alignment)
    aligned++;
               
  return aligned;
}


/****** detect true window position and adapt overlay to it *******/
void dxr3_overlay_adapt_area(dxr3_driver_t *this,
	 int dest_x, int dest_y, int dest_width, int dest_height)
{
	XWindowAttributes a;
	Window junkwin;
	int rx, ry;

	XLockDisplay(this->display);

	XSetForeground(this->display, this->gc, this->color.pixel);
	XGetWindowAttributes(this->display, this->win, &a);
 	XTranslateCoordinates (this->display, this->win, a.root,
				 dest_x, dest_y, &rx, &ry, &junkwin);

	XUnlockDisplay(this->display);
	
	this->xpos = rx; this->ypos = ry;
	this->width = dest_width; this->height = dest_height;

	dxr3_overlay_set_window(&this->overlay, this->xpos, this->ypos,
	 			 this->width, this->height);
}


/****** Allocate keycolor in the current palette ***********/
void dxr3_get_keycolor(dxr3_driver_t *this)
{
	this->color.red   = ((this->overlay.colorkey >> 16) & 0xff) * 256;
	this->color.green = ((this->overlay.colorkey >>  8) & 0xff) * 256;
	this->color.blue  = ((this->overlay.colorkey      ) & 0xff) * 256;

	XAllocColor(this->display, DefaultColormap(this->display,0), &this->color);
}


/******* Read dxr3 configuration data from ~/.xinerc  **********
 *  overlay setup data is read from ~/.overlay/res* in the     *
 *   		     overlay section below  		       *
 ***************************************************************/	
void dxr3_read_config(dxr3_driver_t *this)
{
	char* str;
	config_values_t *config=this->config;
	
	if (ioctl(this->fd_control, EM8300_IOCTL_GETBCS, &this->bcs))
		fprintf(stderr, "dxr3_vo: cannot read bcs values (%s)\n",
		 strerror(errno));

	this->bcs.contrast = config->register_range(config, "dxr3.contrast", this->bcs.contrast,100,900,"Dxr3: contrast control",NULL,NULL,NULL);
	this->bcs.saturation = config->register_range(config, "dxr3.saturation", this->bcs.saturation,100,900,"Dxr3: saturation control",NULL,NULL,NULL);
	this->bcs.brightness = config->register_range(config, "dxr3.brightness", this->bcs.brightness,100,900,"Dxr3: brightness control",NULL,NULL,NULL);

	this->fullscreen_rectangle = config->register_bool(config, "dxr3.fullscreen_rectangle",0,"Dxr3: Fullscreen Rectangle Mode",NULL,NULL,NULL);

	this->vo_driver.set_property(&this->vo_driver,
	 VO_PROP_ASPECT_RATIO, ASPECT_FULL);

	str = config->register_string(config, "dxr3.videoout_mode", "tv", "Dxr3: videoout mode (tv or overlay)", NULL,NULL,NULL);

	if (!strcasecmp(str, "tv")) {
		this->overlay_enabled=0;
		this->tv_switchable=0;  /* don't allow on-the-fly switching */		
	} else if (!strcasecmp(str, "overlay")) {
		this->tv_mode = EM8300_VIDEOMODE_DEFAULT;
		fprintf(stderr, "dxr3_vo: setting up overlay mode\n");
		if (dxr3_overlay_read_state(&this->overlay) == 0) {
			this->overlay_enabled = 1;
			this->tv_switchable=1;	
			str = config->register_string(config, "dxr3.keycolor", "0x80a040", "Dxr3: overlay colourkey value",NULL,NULL,NULL);

			sscanf(str, "%x", &this->overlay.colorkey);

			str = config->register_string(config, "dxr3.color_interval", "50.0", "Dxr3: overlay colourkey range","A greater value widens the search range for the overlay keycolor",NULL,NULL);

			sscanf(str, "%f", &this->overlay.color_interval);
		} else {
			fprintf(stderr, "dxr3_vo: please run autocal, overlay disabled\n");
			this->overlay_enabled=0;
			this->tv_switchable=0;
		}
	}	
	str = config->register_string(config, "dxr3.preferred_tvmode", "default", "Dxr3 preferred tv mode - PAL, PAL60, NTSC or default",NULL,NULL,NULL);

	if (!strcasecmp(str, "ntsc")) {
		this->tv_mode = EM8300_VIDEOMODE_NTSC;
		fprintf(stderr, "dxr3_vo: setting tv_mode to NTSC\n");
	} else if (!strcasecmp(str, "pal")) {
		this->tv_mode = EM8300_VIDEOMODE_PAL;
		fprintf(stderr, "dxr3_vo: setting tv_mode to PAL 50Hz\n");
	} else if (!strcasecmp(str, "pal60")) {
		this->tv_mode = EM8300_VIDEOMODE_PAL60;
		fprintf(stderr, "dxr3_vo: setting tv_mode to PAL 60Hz\n");
	} else {
		this->tv_mode = EM8300_VIDEOMODE_DEFAULT;
	}

	
	if (this->tv_mode != EM8300_VIDEOMODE_DEFAULT)
		if (ioctl(this->fd_control, EM8300_IOCTL_SET_VIDEOMODE, &this->tv_mode))
			fprintf(stderr, "dxr3_vo: setting video mode failed.");
}


/******** is this window fullscreen? ************/
static int is_fullscreen(dxr3_driver_t *this)
{
	XWindowAttributes a;

	XGetWindowAttributes(this->display, this->win, &a);
	/* this is a good place for gathering the with and height
	 * although it is a mis-use for is_fullscreen */
	this->width  = a.width;
	this->height = a.height;
	
	return a.x==0 && a.y==0 &&
	 a.width  == this->overlay.screen_xres &&
	 a.height == this->overlay.screen_yres;
}


/******* Experimental zoom function for tvout only *********
 *	     (mis)uses the dxr3 dicom function	           *
 ***********************************************************/
static void dxr3_zoomTV(dxr3_driver_t *this)
{
       	em8300_register_t frame, visible, update;
       	frame.microcode_register=1; 	/* Yes, this is a MC Reg */
       	visible.microcode_register=1; 	/* Yes, this is a MC Reg */       	
       	update.microcode_register=1;

	/* change left <- */       	
	       	frame.microcode_register=1; 	/* Yes, this is a MC Reg */
	       	visible.microcode_register=1; 	/* Yes, this is a MC Reg */       	
	       	frame.reg = 93; // dicom frame left
	       	visible.reg = 97; //dicom visible left
	       	update.reg = 65; //dicom_update
		update.val=1;
		frame.val=0x10;
		visible.val=0x10;
			
		ioctl(this->fd_control, EM8300_IOCTL_WRITEREG, &frame);
		ioctl(this->fd_control, EM8300_IOCTL_WRITEREG, &visible);
		ioctl(this->fd_control, EM8300_IOCTL_WRITEREG, &update);

	/* change right -> */       	
	       	frame.microcode_register=1; 	/* Yes, this is a MC Reg */
	       	visible.microcode_register=1; 	/* Yes, this is a MC Reg */       	
	       	update.reg = 94; // dicom frame right
	       	visible.reg = 98; //dicom visible right
	       	update.reg = 65; //dicom_update
		update.val=1;
		frame.val=0x10; 
		visible.val= 968;
			
		ioctl(this->fd_control, EM8300_IOCTL_WRITEREG, &frame);
		ioctl(this->fd_control, EM8300_IOCTL_WRITEREG, &visible);
		ioctl(this->fd_control, EM8300_IOCTL_WRITEREG, &update);

}

int dxr3_get_property (vo_driver_t *this_gen, int property)
{
	dxr3_driver_t *this = (dxr3_driver_t *) this_gen;
	int val=0;

	switch (property) {
	case VO_PROP_SATURATION:
		val = this->bcs.saturation;
		break;
		
	case VO_PROP_CONTRAST:
		val = this->bcs.contrast;
		break;
		
	case VO_PROP_BRIGHTNESS:
		val = this->bcs.brightness;
		break;

	case VO_PROP_ASPECT_RATIO:
		val = this->aspectratio;
		break;
	
	case VO_PROP_COLORKEY:
		val = this->overlay.colorkey;
		break;
	case VO_PROP_ZOOM_X:
	case VO_PROP_ZOOM_Y:
	case VO_PROP_TVMODE:
		break;

	default:
		val = 0;
		fprintf(stderr, "dxr3_vo: property %d not implemented!\n", property);
	}

	return val;
}

int dxr3_set_property (vo_driver_t *this_gen, 
			      int property, int value)
{
	dxr3_driver_t *this = (dxr3_driver_t *) this_gen;
	int val, bcs_changed = 0;
	int fullscreen;

	switch (property) {
	case VO_PROP_SATURATION:
		this->bcs.saturation = value;
		bcs_changed = 1;
		break;
	case VO_PROP_CONTRAST:
		this->bcs.contrast = value;
		bcs_changed = 1;
		break;
	case VO_PROP_BRIGHTNESS:
		this->bcs.brightness = value;
		bcs_changed = 1;
		break;
	case VO_PROP_ASPECT_RATIO:
		/* xitk-ui just increments the value, so we make
		 * just a two value "loop"
		 */
		if (value > ASPECT_FULL)
			value = ASPECT_ANAMORPHIC;
		this->aspectratio = value;
		fullscreen = is_fullscreen(this);

		if (value == ASPECT_ANAMORPHIC) {
			fprintf(stderr, "dxr3_vo: setting aspect ratio to anamorphic\n");
			if (!this->overlay_enabled || fullscreen)
				val = EM8300_ASPECTRATIO_16_9;
			else /* The overlay window can adapt to the ratio */
				val = EM8300_ASPECTRATIO_4_3;
			this->desired_ratio = 16.0/9.0;
		} else {
			fprintf(stderr, "dxr3_vo: setting aspect ratio to full\n");
			val = EM8300_ASPECTRATIO_4_3;
			this->desired_ratio = 4.0/3.0;
		}

		if (ioctl(this->fd_control, EM8300_IOCTL_SET_ASPECTRATIO, &val))
			fprintf(stderr, "dxr3_vo: failed to set aspect ratio (%s)\n",
			 strerror(errno));
		
		if (this->overlay_enabled && !fullscreen){
			int foo;
			this->request_dest_size(this->user_data, this->width,
			 this->width/this->desired_ratio, &foo, &foo, &foo, &foo);
		}
		break;
	case VO_PROP_COLORKEY:
		fprintf(stderr, "dxr3_vo: VO_PROP_COLORKEY not implemented!");
		this->overlay.colorkey = val;
		break;
	case VO_PROP_ZOOM_X: 
		if(!this->overlay_enabled){  /* TV-out only */
		  if(value==1){
			fprintf(stderr, "dxr3_vo: enabling 16:9 zoom\n");
			val=EM8300_ASPECTRATIO_4_3;
			if (ioctl(this->fd_control, EM8300_IOCTL_SET_ASPECTRATIO, &val))
				fprintf(stderr, "dxr3_vo: failed to set aspect ratio (%s)\n",
				 strerror(errno));
			dxr3_zoomTV(this);
		  }else if (value==-1){
			fprintf(stderr, "dxr3_vo: disabling 16:9 zoom\n");		
			if (ioctl(this->fd_control, EM8300_IOCTL_SET_ASPECTRATIO, &this->aspectratio))
				fprintf(stderr, "dxr3_vo: failed to set aspect ratio (%s)\n",
				 strerror(errno));
		  }
		}
		break;
		
	case VO_PROP_TVMODE: {
		  	/* Use meta-v to cycle TV formats */
		  	static int newmode;
		  	newmode++;
		  	if (newmode>EM8300_VIDEOMODE_LAST)
		  		newmode=EM8300_VIDEOMODE_PAL;
				fprintf(stderr, "dxr3_vo: Changing TVMode to ");
				if(newmode==EM8300_VIDEOMODE_PAL)
					fprintf(stderr, "PAL\n");
				if(newmode==EM8300_VIDEOMODE_PAL60)
					fprintf(stderr, "PAL60\n");
				if(newmode==EM8300_VIDEOMODE_NTSC)
					fprintf(stderr, "NTSC\n");
		  	if (ioctl(this->fd_control, EM8300_IOCTL_SET_VIDEOMODE, &newmode))
			fprintf(stderr, "dxr3_vo: setting video mode failed.");
		}
		break;
	default:
		break;
	}

	if (bcs_changed){
		if (ioctl(this->fd_control, EM8300_IOCTL_SETBCS, &this->bcs))
			fprintf(stderr, "dxr3_vo: bcs set failed (%s)\n",
			 strerror(errno));
		this->config->update_num(this->config, "dxr3.contrast", this->bcs.contrast);
		this->config->update_num(this->config, "dxr3.saturation", this->bcs.saturation);
		this->config->update_num(this->config, "dxr3.brightness", this->bcs.brightness);
	}			 
			 
	return value;
}

void dxr3_get_property_min_max (vo_driver_t *this_gen, 
 				int property, int *min, int *max)
{
	/* dxr3_driver_t *this = (dxr3_driver_t *) this_gen;  */

	switch (property) {
	case VO_PROP_SATURATION:
	case VO_PROP_CONTRAST:
	case VO_PROP_BRIGHTNESS:
		*min = 0;
		*max = 1000;
		break;

	default:
		*min = 0;
		*max = 0;
	}
}

static void dxr3_translate_gui2video(dxr3_driver_t *this,
				     int x, int y,
				     int *vid_x, int *vid_y)
{
	x = x * this->video_width / this->width;
	y = y * this->video_height / this->height;
	*vid_x = x;
	*vid_y = y;
}

int dxr3_gui_data_exchange (vo_driver_t *this_gen, 
				 int data_type, void *data)
{
	dxr3_driver_t *this = (dxr3_driver_t*) this_gen;

	
	if (!this->overlay_enabled && !this->tv_switchable) return 0;

	switch (data_type) {
	case GUI_DATA_EX_DEST_POS_SIZE_CHANGED:{
			x11_rectangle_t *area = (x11_rectangle_t*) data;
			dxr3_overlay_adapt_area(this, area->x, area->y, area->w, area->h);
  			
			if(is_fullscreen(this) && this->fullscreen_rectangle)
				dxr3_overlay_set_mode(&this->overlay,EM8300_OVERLAY_MODE_RECTANGLE);
			else if (this->fullscreen_rectangle)
				dxr3_overlay_set_mode(&this->overlay,EM8300_OVERLAY_MODE_OVERLAY);
		}
		break;
	case GUI_DATA_EX_EXPOSE_EVENT:{
			XLockDisplay(this->display);
			XFillRectangle(this->display, this->win,
				 this->gc, 0, 0, this->width, this->height);
			XUnlockDisplay(this->display);
		}
		break;
	case GUI_DATA_EX_DRAWABLE_CHANGED:{
			XWindowAttributes a;
			this->win = (Drawable) data;
			this->gc = XCreateGC(this->display, this->win, 0, NULL);
			XGetWindowAttributes(this->display, this->win, &a);
			dxr3_set_property((vo_driver_t*) this,
				 VO_PROP_ASPECT_RATIO, this->aspectratio);
		}
		break;
	case GUI_DATA_EX_TRANSLATE_GUI_TO_VIDEO:{
			int x1, y1, x2, y2;
			x11_rectangle_t *rect = data;

			dxr3_translate_gui2video(this, rect->x, rect->y,
						 &x1, &y1);
			dxr3_translate_gui2video(this, rect->w, rect->h,
						 &x2, &y2);
			rect->x = x1;
			rect->y = y1;
			rect->w = x2-x1;
			rect->h = y2-y1;
		}
		break;
	case GUI_DATA_EX_VIDEOWIN_VISIBLE:{ 
			int window_showing;
			(int *)window_showing = (int *)data;
			if(!window_showing){
				fprintf(stderr, "dxr3_vo: Hiding VO window and diverting video to TV\n");
				dxr3_overlay_set_mode(&this->overlay, EM8300_OVERLAY_MODE_OFF );
				this->overlay_enabled=0;
			}else{
				fprintf(stderr, "dxr3_vo: Using VO window for overlaying video\n");
				dxr3_overlay_set_mode(&this->overlay, EM8300_OVERLAY_MODE_OVERLAY );			
				this->overlay_enabled=1;
			}
		dxr3_set_property((vo_driver_t*) this,
			 VO_PROP_ASPECT_RATIO, this->aspectratio);
		break;
		}

	default:
		return -1;
	}
	return 0;
}

/******** detect screen resolution and colour depth **********/
void gather_screen_vars(dxr3_driver_t *this, x11_visual_t *vis)
{
	int scrn;
#ifdef HAVE_XINERAMA
	int screens;
	int dummy_a, dummy_b;
	XineramaScreenInfo *screeninfo = NULL;
#endif

	this->win = vis->d;
	this->display = vis->display;
	this->user_data = vis->user_data;
	this->gc = XCreateGC(this->display, this->win, 0, NULL);
	scrn = DefaultScreen(this->display);

	/* Borrowed from xine-ui in order to detect fullscreen */
#ifdef HAVE_XINERAMA
	if (XineramaQueryExtension(this->display, &dummy_a, &dummy_b) &&
	 (screeninfo = XineramaQueryScreens(this->display, &screens)) &&
	 XineramaIsActive(this->display))
	{
		this->overlay.screen_xres = screeninfo[0].width;
		this->overlay.screen_yres = screeninfo[0].height;
	} else
#endif
	{
		this->overlay.screen_xres = DisplayWidth(this->display, scrn);
		this->overlay.screen_yres = DisplayHeight(this->display, scrn);
	}

	this->overlay.screen_depth = DisplayPlanes(this->display, scrn);
	this->request_dest_size = (void *)vis->request_dest_size;
	printf("xres %d yres %d depth %d\n", this->overlay.screen_xres, this->overlay.screen_yres, this->overlay.screen_depth);
}

/**************************************************************************
 * Overlay initialisation and other overlay_specific functions 		  *
 **************************************************************************/
 
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
	/* TODO: catch errors */

	this->fd_control = fd;
	dxr3_overlay_set_screen(this);
	dxr3_overlay_set_window(this, 1,1, 2,2);
	dxr3_overlay_set_keycolor(this);
 	dxr3_overlay_set_attributes(this);
	dxr3_overlay_set_mode(this, EM8300_OVERLAY_MODE_OVERLAY);
}
