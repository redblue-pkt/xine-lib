/*
 * spudec.c
 *
 * Copyright (C) Rich Wareham <rjw57@cam.ac.uk> - Jan 2001
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
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with GNU Make; see the file COPYING. If not, write to
 * the Free Software Foundation, 
 *
 */

#include "spudec.h"

#include "xine_internal.h"
#include "utils.h"
#include "metronom.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <fcntl.h>
#include <unistd.h>

typedef struct _spudec_globals {
  vo_image_buffer_t *overlay;
  vo_image_buffer_t *mask;
  int                width, height;
  int                format;

  int                bInitialised;

  int                state;

  spudec_geometry    geom;

  /* The current packet we are assembling, not decoding */
  unsigned char     *packet; 
  int                packet_size;
  uint32_t           pts; /* PTS of packet */

  uint32_t           lifetime; /* Lifetime of currently displayed SPU in pts */
  uint32_t           displayPTS; /* The PTS when the last SPU was displayed. */

  uint32_t           lastPTS;

  clut_t            *clut;
} spudec_globals;

static spudec_globals gSpudec;

clut_t *palette[4] = {
  NULL, NULL, NULL, NULL
};

uint8_t alpha[4] = {
  0xff, 0x00, 0x00, 0x00
};

#ifdef BIG_ENDIAN
static uint32_t default_palette[32] = {
  0x80801000, 0x80801000, 0x80808400, 0x8080eb00,
  0x80801000, 0x80801000, 0x80808400, 0x8080eb00,
  0x80801000, 0x80801000, 0x80808400, 0x8080eb00,
  0x80801000, 0x80801000, 0x80808400, 0x8080eb00,
};

#else

static uint32_t default_palette[32] = {
  0x00108080, 0x00108080, 0x00848080, 0x00eb8080,
  0x00108080, 0x00108080, 0x00848080, 0x00eb8080,
  0x00108080, 0x00108080, 0x00848080, 0x00eb8080,
  0x00108080, 0x00108080, 0x00848080, 0x00eb8080
};
#endif

static clut_t* default_clut = (clut_t*) default_palette;

/* Maximum packets we can keep in the queue. Should be fine */
#define MAX_PACKETS 200

typedef struct {
  unsigned char *packet; /* The actual packet of data */
  uint32_t       pts;    /* The PTS of the packet */
  uint16_t       size;   /* The packet size */
} spudec_packet;

/* Implement the SPU packet queue as a ring queuefer */
spudec_packet* spudec_packet_queue[MAX_PACKETS];
int16_t spudec_queue_size = 0;  /* Queue length (items) */
int16_t spudec_queue_pos = 0;   /* Start of queue position */

/* Forward declarations */
void spudec_process_packet(unsigned char *packet, int size);

/* Pushes a packet on the end of the queue */
void spudec_queue_packet(spudec_packet *packet) {
  if(spudec_queue_size + 1 > MAX_PACKETS) {
    /* Too many packets */
    printf("spudec: Too many packets.\n");
    return;
  }

  spudec_packet_queue[(spudec_queue_pos + spudec_queue_size) % MAX_PACKETS] = packet;
  spudec_queue_size++;
}

/* Gets the next packet but does /not/ remove it */
spudec_packet* spudec_peek_next_packet() {
  if(spudec_queue_size <= 0) {
    /* No packet in queue */
    printf("spudec: No more packets.\n");
    return NULL;
  }

  return spudec_packet_queue[spudec_queue_pos];
}

/* Like peek_next but removes the packet from the queue */
spudec_packet* spudec_get_next_packet() {
  spudec_packet *packet;

  if((packet = spudec_peek_next_packet()) != NULL) {
    spudec_queue_pos = (spudec_queue_pos + 1) % MAX_PACKETS;
    spudec_queue_size --;
  }

  return packet;
}

void spudec_init(clut_t *clut) {
  gSpudec.bInitialised = 1; 

  spudec_reset();

  if(clut == NULL) {
    gSpudec.clut = default_clut;
  } else {
    gSpudec.clut = clut;
  }

  palette[0] = palette[1] = palette[2] = palette[3] = &(gSpudec.clut[0]);
}

void spudec_tick()
{
  uint32_t pts;
  if(!gSpudec.bInitialised)
    return;

  pts = metronom_got_spu_packet(0);

  /* See if we have any SPUs queued */
  if(spudec_queue_size != 0) {
    spudec_packet *p;

    p = spudec_peek_next_packet();
    if(pts >= p->pts) {
      /* Process */
      p = spudec_get_next_packet();

      spudec_process_packet(p->packet, p->size);     
      /* Assume it was displayed correctly */
      gSpudec.displayPTS = p->pts;
      
      free(p->packet);
      free(p);
    } 
 }

  if((gSpudec.geom.bIsVisible) && (pts - gSpudec.lifetime >= gSpudec.displayPTS)) {
    gSpudec.geom.bIsVisible = 0;
    gSpudec.lifetime = 0;
    gSpudec.lastPTS = pts;
    return; 
  }

  if(pts < gSpudec.lastPTS) {
    /* Something screwey. */
    gSpudec.lastPTS = pts;
    return;
  }

  gSpudec.lastPTS = pts;
}

spudec_geometry* spudec_get_geometry()
{
  return &(gSpudec.geom);
}

int spudec_set_images(vo_image_buffer_t* overlay,
                      vo_image_buffer_t* mask,
                      int width, int height,
                      int format)
{
  if(format != IMGFMT_YV12) {
    printf("Error, SPUDEC only supports YV12 overlays.\n");
    gSpudec.bInitialised = 0;
    
    return 0;
  }

  gSpudec.overlay = overlay;
  gSpudec.mask    = mask;
  gSpudec.width   = width;
  gSpudec.height  = height;
  gSpudec.format  = format;
  gSpudec.geom.bIsVisible = 0;

  /* Clear images initially */
  if(gSpudec.format == IMGFMT_YV12) {
    /* Set initial image to empty & clear mask */
    memset(mask->mem[0], 0xff, (gSpudec.width*gSpudec.height));
    memset(overlay->mem[0], 0x00, (gSpudec.width*gSpudec.height));
    memset(mask->mem[1], 0xff, (gSpudec.width*gSpudec.height) >> 2);
    memset(overlay->mem[1], 0x00, (gSpudec.width >> 1)*(gSpudec.height >> 1));
    memset(mask->mem[2], 0xff, (gSpudec.width >> 1)*(gSpudec.height >> 1));
    memset(overlay->mem[2], 0x00, (gSpudec.width >> 1)*(gSpudec.height >> 1)); 
  }

  gSpudec.geom.start_col = 0;
  gSpudec.geom.end_col = gSpudec.width-1;
  gSpudec.geom.start_row = 0;
  gSpudec.geom.end_row = gSpudec.height-1;

  return gSpudec.bInitialised = 1;
}

#define nibble(data, index) ((index & 1) ? data[index >> 1] & 0xf : (data[index >> 1] >> 4) & 0xf)

void spudec_process_data(unsigned char *data, int size, int d1, int d2)
{
  /* This does the 'hard' work of processing the image data */

  long off,line_base, line_base2,y,na,nb;
  int n; /* The code word */

  na = d1<<1;
  nb = d2<<1;

  /* Align on even row */
  if (gSpudec.geom.start_row & 1) {
    gSpudec.geom.start_row--;
    gSpudec.geom.end_row--;
  }
  if (gSpudec.geom.start_col & 1) {
    gSpudec.geom.start_col--;
    gSpudec.geom.end_col--;
  }
  y = gSpudec.geom.start_row;

  while(y <= gSpudec.geom.end_row) {
    line_base = gSpudec.width * y + gSpudec.geom.start_col;
    line_base2 = (gSpudec.width>>1) * (y>>1) + (gSpudec.geom.start_col >> 1);

    off = 0;
    do {
      int num;
      clut_t *col;

      n = nibble(data, na);
      na++;
      if(n < 0x4) {
        n = (n<<4) | nibble(data, na);
        na++;
        if(n < 0x10) { 
          n = (n<<4) | nibble(data, na);
          na++;
          if(n < 0x40) {
            n = (n<<4) | nibble(data, na);
            na++;
            if(n < 0x100)
              n = 0; /* Carriage return */
          }
        }
      } 
      // printf("Code: 0x%04x\n",n);

      num = n >> 2; col = palette[n & 0x3];
      if(col == NULL) {
        printf("Error in palette\n");
      }
      
      if(num != 0) {
        if(alpha[n & 0x3] & 0x80) {
          memset(&(gSpudec.mask->mem[0][line_base + off]), 0x00, num);
          memset(&(gSpudec.overlay->mem[0][line_base + off]), col->y, num);
          memset(&(gSpudec.mask->mem[1][line_base2 + (off>>1)]), 0x00, num>>1);     
          memset(&(gSpudec.overlay->mem[1][line_base2 + (off>>1)]), col->cr, num>>1); 
          memset(&(gSpudec.mask->mem[2][line_base2 + (off>>1)]), 0x00, num>>1);     
          memset(&(gSpudec.overlay->mem[2][line_base2 + (off>>1)]), col->cb, num>>1); 
        } else {
          memset(&(gSpudec.mask->mem[0][line_base + off]), 0xff, num);
          memset(&(gSpudec.overlay->mem[0][line_base + off]), 0x00, num);
          memset(&(gSpudec.mask->mem[1][line_base2 + (off>>1)]), 0xff, num>>1);     
          memset(&(gSpudec.overlay->mem[1][line_base2 + (off>>1)]), 0x00, num>>1); 
          memset(&(gSpudec.mask->mem[2][line_base2 + (off>>1)]), 0xff, num>>1);     
          memset(&(gSpudec.overlay->mem[2][line_base2 + (off>>1)]), 0x00, num>>1); 
        }
      }
      off+=num;
    } while((n != 0) && (off <= gSpudec.geom.end_col - gSpudec.geom.start_col));

    if((n == 0) && (off <= gSpudec.geom.end_col - gSpudec.geom.start_col)) {
      /* Clear to end of line if carriage return */
      int len = gSpudec.geom.start_col + gSpudec.geom.end_col - off;
      memset(&(gSpudec.mask->mem[0][line_base + off]), 0xff, len);     
      memset(&(gSpudec.overlay->mem[0][line_base + off]), 0x00, len); 
      memset(&(gSpudec.mask->mem[0][line_base + off]) + gSpudec.width, 0xff, len);     
      memset(&(gSpudec.overlay->mem[0][line_base + off]) + gSpudec.width, 0x00, len); 
      memset(&(gSpudec.mask->mem[1][line_base2 + (off>>1)]), 0xff, len>>1);     
      memset(&(gSpudec.overlay->mem[1][line_base2 + (off>>1)]), 0x00, len>>1); 
      memset(&(gSpudec.mask->mem[2][line_base2 + (off>>1)]), 0xff, len>>1);     
      memset(&(gSpudec.overlay->mem[2][line_base2 + (off>>1)]), 0x00, len>>1); 
    }

    if((na & 1))
      na ++; /* Re-align */

    line_base += gSpudec.width;
    y++;
    if (y > gSpudec.geom.end_row)
      break;

    off = 0;
    do {
      int num;
      clut_t *col;

      n = nibble(data, nb);
      nb++;
      if(n < 0x4) {
        n = (n<<4) | nibble(data, nb);
        nb++;
        if(n < 0x10) { 
          n = (n<<4) | nibble(data, nb);
          nb++;
          if(n < 0x40) {
            n = (n<<4) | nibble(data, nb);
            nb++;
            if(n < 0x100)
              n = 0; /* Carriage return */
          }
        }
      } 
      // printf("Code: 0x%04x\n",n);

      num = n >> 2; col = palette[n & 0x3];
      if(col == NULL) {
        printf("Error in palette\n");
      }
      
      if(num != 0) {
        if(alpha[n & 0x3] & 0x80) {
          memset(&(gSpudec.mask->mem[0][line_base + off]), 0x00, num);
          memset(&(gSpudec.overlay->mem[0][line_base + off]), col->y, num);
        } else {
          memset(&(gSpudec.mask->mem[0][line_base + off]), 0xff, num);
          memset(&(gSpudec.overlay->mem[0][line_base + off]), 0x00, num);
        }
      }
      off+=num;
    } while((n != 0) && (off <= gSpudec.geom.end_col - gSpudec.geom.start_col));

    if((n == 0) && (off <= gSpudec.geom.end_col - gSpudec.geom.start_col)) {
      /* Clear to end of line if carriage return */
      int len = gSpudec.geom.start_col + gSpudec.geom.end_col - off;
      memset(&(gSpudec.mask->mem[0][line_base + off]), 0xff, len);     
      memset(&(gSpudec.overlay->mem[0][line_base + off]), 0x00, len); 
    }

    if((nb & 1))
      nb ++; /* Re-align */

    y++;
  }
}

void spudec_process_control(unsigned char *control, int size, int* d1, int* d2)
{
  int off = 2;
  int a,b; /* Temporary vars */

  do {
    int type = control[off];
    off++;

    switch(type) {
    case 0x00:
      /* Menu ID, 1 byte */
      break;
    case 0x01:
      /* Start display */
      gSpudec.geom.bIsVisible = 1;
      break;
    case 0x03:
      /* Palette */
      palette[3] = &(gSpudec.clut[(control[off] >> 4)]);
      palette[2] = &(gSpudec.clut[control[off] & 0xf]);
      palette[1] = &(gSpudec.clut[(control[off+1] >> 4)]);
      palette[0] = &(gSpudec.clut[control[off+1] & 0xf]);
      off+=2;
      break;
    case 0x04:
      /* Alpha */
      alpha[3] = control[off] & 0xf0;
      alpha[2] = (control[off] & 0xf) << 4;
      alpha[1] = control[off+1] & 0xf0;
      alpha[0] = (control[off+1] & 0xf) << 4;
      off+=2;
      break;
    case 0x05:
      /* Co-ords */
      a = (control[off] << 16) + (control[off+1] << 8) + control[off+2];
      b = (control[off+3] << 16) + (control[off+4] << 8) + control[off+5];

      gSpudec.geom.start_col = a >> 12;
      gSpudec.geom.end_col = a & 0xfff;
      gSpudec.geom.start_row = b >> 12;
      gSpudec.geom.end_row = b & 0xfff;

      off+=6;
      break;
    case 0x06:
      /* Graphic lines */
      *(d1) = (control[off] << 8) + control[off+1];
      *(d2) = (control[off+2] << 8) + control[off+3];
      off+=4;
      break;
    case 0xff:
      /* All done, bye-bye */
      return;
      break;
    default:
      printf("spudec: Error determining control type 0x%02x.\n",type);
      return;
      break;
    }

    /* printf("spudec: Processsed control type 0x%02x.\n",type); */
  } while(off < size);
}

void spudec_process_packet(unsigned char *packet, int size)
{
  int x0, x1;
  int d1, d2;

  /* Check packet */
  if((packet[0] << 8) + packet[1] != size) {
    printf("Packet size mismatch:\n");
    printf("Packet reports size 0x%04x\n", (packet[0] << 8) + packet[1]);
    printf("I reckon            0x%04x\n", size);
    return;
  }

  x0 = (packet[2] << 8) + packet[3];
  x1 = (packet[x0+2] << 8) + packet[x0+3];

  /* /Another/ sanity check. */
  if((packet[x1+2]<<8) + packet[x1+3] != x1) {
    printf("spudec: Incorrect packet.\n");
    return;
  }

  /* End sequence, FIXME: why do we need the division by 2? */
  gSpudec.lifetime = (metronom_get_video_rate() >> 1)* ((packet[x1]<<8) + packet[x1+1]);

  d1 = d2 = -1;
  spudec_process_control(packet + x0 + 2, x1-x0-2, &d1, &d2);

  if((d1 != -1) && (d2 != -1)) {
    spudec_process_data(packet, x0, d1, d2);
  }
}

void spudec_decode(unsigned char *data, int size, uint32_t pts) {
  if(!gSpudec.bInitialised)
    return;

  if(gSpudec.packet == NULL) {
    if(pts != 0) {
      /* Allocate a packet buffer */
      gSpudec.packet = xmalloc((data[0] << 8) + data[1]);
      gSpudec.pts = pts;
      
      if(gSpudec.packet == NULL) {
        printf("Error allocating packet buffer.\n");
        return;
      }
      
      gSpudec.packet_size = 0;
    } else {
      printf("spudec: Error, we are half way through a packet I don't know\n");
    }
  }

  /* Prevent buffer overruns */
  if(gSpudec.packet_size >= 2) { /* If the /packet/ knows how big it is */
    if((gSpudec.packet_size + size) >
       (gSpudec.packet[0]<<8) + gSpudec.packet[1]) {
      printf("spudec: Mismatched buffer size (0x%04x to big), truncating.\n",
             (gSpudec.packet_size + size) - 
             ((gSpudec.packet[0]<<8) + gSpudec.packet[1]));
      size = (gSpudec.packet[0]<<8) + gSpudec.packet[1] - gSpudec.packet_size;
    }
  }


  memcpy(gSpudec.packet + gSpudec.packet_size, data, size);
  gSpudec.packet_size += size;

  if(gSpudec.packet_size >= (gSpudec.packet[0]<<8) + gSpudec.packet[1]) {
    /* If packet complete then queue */
    spudec_packet *p;

    p = xmalloc(sizeof(spudec_packet));
    p->packet = gSpudec.packet;
    p->size = gSpudec.packet_size;
    p->pts = gSpudec.pts;

    spudec_queue_packet(p);
    
    gSpudec.packet = NULL;
    gSpudec.packet_size = -1;
    gSpudec.pts = 0;
  }
}

void spudec_reset() {
  /* Clear any packet being assembled */
  if(gSpudec.packet != NULL) {
    gSpudec.packet_size = 0;
    free(gSpudec.packet);
    gSpudec.packet = NULL;
    gSpudec.pts = 0;
  }

  /* Remove any current subtitle */
  gSpudec.geom.bIsVisible = 0;
  gSpudec.lifetime = 0;

  /* Clear packet queue */
  while(spudec_queue_size > 0) {
    spudec_packet *p = spudec_get_next_packet();

    free(p->packet);
    free(p);
  }
}

void spudec_overlay_yuv (uint8_t *y, uint8_t *u, uint8_t *v) {

  /* 
   * Mix in SPU 
   */
  
  /* Tick SPUdec */
  spudec_tick();
  
  if(gVO.bOverlayImage) {
    /* This code is pretty nasty but quite quick which is important here! */
    /* APPROACH: Since 32bit processors hande data, well 32bits at a time,
     * we overlay the image word by word rather than byte by byte and then
     * tidy up the odd bytes at the end. This effectively cuts the number of
     * loop itterations by a factor of 4. */
    
    /* FIXME: Optimise for 64bit machines as well? */
    
    if((gVO.spu_geom->bIsVisible) && 
       (gVO.format == IMGFMT_YV12)) {
      /* Overlay the image. */
      long i, off;
      uint8_t  *img_l_8,  *ovl_l_8,  *msk_l_8;
      uint32_t *img_l_32, *ovl_l_32, *msk_l_32;
      uint8_t  *img_y_8,  *ovl_y_8,  *msk_y_8;
      uint32_t *img_y_32, *ovl_y_32, *msk_y_32;
      uint8_t  *img_v_8,  *ovl_v_8,  *msk_v_8;
      uint32_t *img_v_32, *ovl_v_32, *msk_v_32;

      img_l_8 = img->mem[0];
      ovl_l_8 = gVO.overlay_image->mem[0];
      msk_l_8 = gVO.mask_image->mem[0];
      img_l_32 = ((uint32_t*)img->mem[0]);
      ovl_l_32 = ((uint32_t*)gVO.overlay_image->mem[0]);
      msk_l_32 = ((uint32_t*)gVO.mask_image->mem[0]);
      img_y_8 = img->mem[1];
      ovl_y_8 = gVO.overlay_image->mem[1];
      msk_y_8 = gVO.mask_image->mem[1];
      img_y_32 = ((uint32_t*)img->mem[1]);
      ovl_y_32 = ((uint32_t*)gVO.overlay_image->mem[1]);
      msk_y_32 = ((uint32_t*)gVO.mask_image->mem[1]);
      img_v_8 = img->mem[2];
      ovl_v_8 = gVO.overlay_image->mem[2];
      msk_v_8 = gVO.mask_image->mem[2];
      img_v_32 = ((uint32_t*)img->mem[2]);
      ovl_v_32 = ((uint32_t*)gVO.overlay_image->mem[2]);
      msk_v_32 = ((uint32_t*)gVO.mask_image->mem[2]);

      /* luminance */
      for(i=gVO.width*gVO.spu_geom->start_row; 
	  i<=gVO.width*gVO.spu_geom->end_row; i+=gVO.width) {
	/* i is address of begining of line. */

        /* Firstly, draw the start odd bytes. */
	for(off = i+gVO.spu_geom->start_col; (off & 3) != 0; off++) {
	  if(msk_l_8[off] != 0xff) {
	    img_l_8[off] &= msk_l_8[off];
	    img_l_8[off] |= ovl_l_8[off];
	  }
	}

	/* Now words */
	for(; off<=i+gVO.spu_geom->end_col-3; off+=4) {
	  if(msk_l_32[off>>2] != 0xffffffff) {
	    img_l_32[off>>2] &= msk_l_32[off>>2];
	    img_l_32[off>>2] |= ovl_l_32[off>>2];
	  }
	}
	off -= 4;

        /* Now end odd bytes */
	for(; off<=i+gVO.spu_geom->end_col; off++) {
	  if(msk_l_8[off] != 0xff) {
	    img_l_8[off] &= msk_l_8[off];
	    img_l_8[off] |= ovl_l_8[off];
	  }
	}
      }
      /* colour */
      for(i=(gVO.width>>1)*(gVO.spu_geom->start_row>>1); 
	  i<=(gVO.width>>1)*(gVO.spu_geom->end_row>>1); i+=(gVO.width)>>1) {
	/* i is address of begining of line. */

        /* Firstly, draw the start odd bytes. */
	for(off = i+((gVO.spu_geom->start_col)>>1); (off & 3) != 0; off++) {
	  if(msk_y_8[off] != 0xff) {
	    img_y_8[off] &= msk_y_8[off];
	    img_y_8[off] |= ovl_y_8[off];
	  }
	  if(msk_v_8[off] != 0xff) {
	    img_v_8[off] &= msk_v_8[off];
	    img_v_8[off] |= ovl_v_8[off];
	  }
	}

	/* Now words */
	for(; off<=i+((gVO.spu_geom->end_col)>>1)-3; off+=4) {
	  if(msk_y_32[off>>2] != 0xffffffff) {
	    img_y_32[off>>2] &= msk_y_32[off>>2];
	    img_y_32[off>>2] |= ovl_y_32[off>>2];
	  }
	  if(msk_v_32[off>>2] != 0xffffffff) {
	    img_v_32[off>>2] &= msk_v_32[off>>2];
	    img_v_32[off>>2] |= ovl_v_32[off>>2];
	  }
	}
	off -= 4;

        /* Final end odd bytes */
	for(; off<=i+((gVO.spu_geom->end_col)>>1); off++) {
	  if(msk_y_8[off] != 0xff) {
	    img_y_8[off] &= msk_y_8[off];
	    img_y_8[off] |= ovl_y_8[off];
	  }
	  if(msk_v_8[off] != 0xff) {
	    img_v_8[off] &= msk_v_8[off];
	    img_v_8[off] |= ovl_v_8[off];
	  }
	}
      }
    }
  }
}
