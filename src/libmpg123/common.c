#include <ctype.h>
#include <stdlib.h>
#include <signal.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#include "mpg123.h"

struct parameter mpg123_param = { 1 , 1 , 0 , 0 };

int tabsel_123[2][3][16] = {
   { {0,32,64,96,128,160,192,224,256,288,320,352,384,416,448,},
     {0,32,48,56, 64, 80, 96,112,128,160,192,224,256,320,384,},
     {0,32,40,48, 56, 64, 80, 96,112,128,160,192,224,256,320,} },

   { {0,32,48,56,64,80,96,112,128,144,160,176,192,224,256,},
     {0,8,16,24,32,40,48,56,64,80,96,112,128,144,160,},
     {0,8,16,24,32,40,48,56,64,80,96,112,128,144,160,} }
};

long freqs[9] = { 44100, 48000, 32000,
                  22050, 24000, 16000 ,
                  11025 , 12000 , 8000 };

int            mpg123_bitindex;
unsigned char *mpg123_wordpointer;
unsigned char *pcm_sample;
int            pcm_point = 0;


#define HDRCMPMASK 0xfffffd00

/*
 * the code a header and write the information
 * into the frame structure
 */
int decode_header(struct frame *fr,unsigned long newhead)
{
    if( newhead & (1<<20) ) {
      fr->lsf = (newhead & (1<<19)) ? 0x0 : 0x1;
      fr->mpeg25 = 0;
    }
    else {
      fr->lsf = 1;
      fr->mpeg25 = 1;
    }
    
    fr->lay = 4-((newhead>>17)&3);
    if( ((newhead>>10)&0x3) == 0x3) {
      fprintf(stderr,"Stream error\n");
      exit(1);
    }
    if(fr->mpeg25) {
      fr->sampling_frequency = 6 + ((newhead>>10)&0x3);
    }
    else {
      int dummy;
      fr->sampling_frequency = ((newhead>>10)&0x3) + (fr->lsf*3); 
      dummy = (newhead>>10)&0x3;
      switch (dummy) {
      case 0:
        fr->sample_rate = 44100;
        break;
      case 1:
        fr->sample_rate = 48000;
        break;
      case 2:
        fr->sample_rate = 32000;
        break;
      case 3:
        fprintf (stderr, "invalid sampling rate\n");
        fr->sample_rate = 44100;
        break;
      }
    }

    fr->error_protection = ((newhead>>16)&0x1)^0x1;

    if(fr->mpeg25) /* allow Bitrate change for 2.5 ... */
      fr->bitrate_index = ((newhead>>12)&0xf);

    fr->bitrate_index = ((newhead>>12)&0xf);
    fr->padding   = ((newhead>>9)&0x1);
    fr->extension = ((newhead>>8)&0x1);
    fr->mode      = ((newhead>>6)&0x3);
    fr->mode_ext  = ((newhead>>4)&0x3);
    fr->copyright = ((newhead>>3)&0x1);
    fr->original  = ((newhead>>2)&0x1);
    fr->emphasis  = newhead & 0x3;

    fr->stereo    = (fr->mode == MPG_MD_MONO) ? 1 : 2;

    if(!fr->bitrate_index)
    {
      fprintf(stderr,"Free format not supported.\n");
      return (0);
    }

    switch(fr->lay)
    {
      case 1:
        fr->framesize  = (long) tabsel_123[fr->lsf][0][fr->bitrate_index] * 12000;
        fr->framesize /= freqs[fr->sampling_frequency];
        fr->framesize  = ((fr->framesize+fr->padding)<<2)-4;
        break;
      case 2:
        fr->framesize = (long) tabsel_123[fr->lsf][1][fr->bitrate_index] * 144000;
        fr->framesize /= freqs[fr->sampling_frequency];
        fr->framesize += fr->padding - 4;
        break;
      case 3:
	fr->framesize  = (long) tabsel_123[fr->lsf][2][fr->bitrate_index] * 144000;
	fr->framesize /= freqs[fr->sampling_frequency]<<(fr->lsf);
	fr->framesize = fr->framesize + fr->padding - 4;
        break; 
    default:
      fprintf(stderr,"Sorry, unknown layer type.\n"); 
      return (0);
    }
    return 1;
}

unsigned int getbits(int number_of_bits)
{
  unsigned long rval;

  if(!number_of_bits)
    return 0;

  {
    rval = mpg123_wordpointer[0];
    rval <<= 8;
    rval |= mpg123_wordpointer[1];
    rval <<= 8;
    rval |= mpg123_wordpointer[2];
    rval <<= mpg123_bitindex;
    rval &= 0xffffff;

    mpg123_bitindex += number_of_bits;

    rval >>= (24-number_of_bits);

    mpg123_wordpointer += (mpg123_bitindex>>3);
    mpg123_bitindex &= 7;
  }
  return rval;
}

unsigned int getbits_fast(int number_of_bits)
{
  unsigned long rval;

  {
    rval = mpg123_wordpointer[0];
    rval <<= 8;	
    rval |= mpg123_wordpointer[1];
    rval <<= mpg123_bitindex;
    rval &= 0xffff;
    mpg123_bitindex += number_of_bits;

    rval >>= (16-number_of_bits);

    mpg123_wordpointer += (mpg123_bitindex>>3);
    mpg123_bitindex &= 7;
  }
  return rval;
}

unsigned int get1bit(void)
{
  unsigned char rval;
  rval = *mpg123_wordpointer << mpg123_bitindex;

  mpg123_bitindex++;
  mpg123_wordpointer += (mpg123_bitindex>>3);
  mpg123_bitindex &= 7;

  return rval>>7;
}
