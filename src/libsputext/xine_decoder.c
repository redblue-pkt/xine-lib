/*
 * Copyright (C) 2000-2002 the xine project
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
 * $Id: xine_decoder.c,v 1.39 2002/09/04 23:31:10 guenter Exp $
 *
 * code based on mplayer module:
 *
 * Subtitle reader with format autodetection
 *
 * Written by laaz
 * Some code cleanup & realloc() by A'rpi/ESP-team
 * dunnowhat sub format by szabi
 */

#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <ctype.h>
#include <iconv.h>

#include "buffer.h"
#include "xine_internal.h"
#include "xineutils.h"
#include "osd.h"

/*
#define LOG 1
*/

#define ERR (void *)-1

#define SUB_MAX_TEXT  5

typedef struct {

  int lines;

  unsigned long start;
  unsigned long end;
    
  char *text[SUB_MAX_TEXT];

  osd_object_t *osd;
  
} subtitle_t;


typedef enum {
  SUBTITLE_SIZE_SMALL = 0,
  SUBTITLE_SIZE_NORMAL,
  SUBTITLE_SIZE_LARGE,

  SUBTITLE_SIZE_NUM        /* number of values in enum */
} subtitle_size;

typedef struct sputext_decoder_s {
  spu_decoder_t      spu_decoder;

  xine_t            *xine;

  vo_instance_t     *vo_out;
  int                output_open;

  FILE              *fd;

  float              mpsub_position;  

  int                width;          /* frame width                */
  int                height;         /* frame height               */
  int                font_size;
  int                line_height;
  int                uses_time;  
  int                errs;  
  subtitle_t        *subtitles;
  int                num;            /* number of subtitle structs */
  int                cur;            /* current subtitle           */
  int                format;         /* constants see below        */     
  subtitle_t        *previous_aqt_sub ;

  osd_renderer_t    *renderer;
  osd_object_t      *osd;
  char              *font;
  char              *src_encoding;
  char              *dst_encoding;

  subtitle_size      subtitle_size;
  int64_t            last_subtitle_end; /* no new subtitle before this vpts */
  int                time_offset;       /* offset in 1/100sec to add to vpts */
} sputext_decoder_t;

#define FORMAT_MICRODVD  0
#define FORMAT_SUBRIP    1
#define FORMAT_SUBVIEWER 2
#define FORMAT_SAMI      3
#define FORMAT_VPLAYER   4
#define FORMAT_RT        5
#define FORMAT_SSA       6 /* Sub Station Alpha */
#define FORMAT_DUNNO     7 /*... erm ... dunnowhat. tell me if you know */
#define FORMAT_MPSUB     8 
#define FORMAT_AQTITLE   9 

static int eol(char p) {
  return (p=='\r' || p=='\n' || p=='\0');
}

static inline void trail_space(char *s) {
  int i;
  while (isspace(*s)) 
    strcpy(s, s + 1);
  i = strlen(s) - 1;
  while (i > 0 && isspace(s[i])) 
    s[i--] = '\0';
}


static subtitle_t *sub_read_line_sami(sputext_decoder_t *this, subtitle_t *current) {

  static char line[1001];
  static char *s = NULL;
  char text[1000], *p, *q;
  int state;

  p = NULL;
  current->lines = current->start = current->end = 0;
  state = 0;
  
  /* read the first line */
  if (!s)
    if (!(s = fgets(line, 1000, this->fd))) return 0;
  
  do {
    switch (state) {
      
    case 0: /* find "START=" */
      s = strstr (s, "Start=");
      if (s) {
	current->start = strtol (s + 6, &s, 0) / 10;
	state = 1; continue;
      }
      break;
      
    case 1: /* find "<P" */
      if ((s = strstr (s, "<P"))) { s += 2; state = 2; continue; }
      break;
      
    case 2: /* find ">" */
      if ((s = strchr (s, '>'))) { s++; state = 3; p = text; continue; }
      break;
      
    case 3: /* get all text until '<' appears */
      if (*s == '\0') { break; }
      else if (*s == '<') { state = 4; }
      else if (!strncasecmp (s, "&nbsp;", 6)) { *p++ = ' '; s += 6; }
      else if (*s == '\r') { s++; }
      else if (!strncasecmp (s, "<br>", 4) || *s == '\n') {
	*p = '\0'; p = text; trail_space (text);
	if (text[0] != '\0')
	  current->text[current->lines++] = strdup (text);
	if (*s == '\n') s++; else s += 4;
      }
      else *p++ = *s++;
      continue;
      
    case 4: /* get current->end or skip <TAG> */
      q = strstr (s, "Start=");
      if (q) {
	current->end = strtol (q + 6, &q, 0) / 10 - 1;
	*p = '\0'; trail_space (text);
	if (text[0] != '\0')
	  current->text[current->lines++] = strdup (text);
	if (current->lines > 0) { state = 99; break; }
	state = 0; continue;
      }
      s = strchr (s, '>');
      if (s) { s++; state = 3; continue; }
      break;
    }
    
    /* read next line */
    if (state != 99 && !(s = fgets (line, 1000, this->fd))) 
      return 0;
    
  } while (state != 99);
  
  return current;
}


static char *sub_readtext(char *source, char **dest) {
  int len=0;
  char *p=source;
  
  while ( !eol(*p) && *p!= '|' ) {
    p++,len++;
  }
  
  *dest= (char *)xine_xmalloc (len+1);
  if (!dest) 
    return ERR;
  
  strncpy(*dest, source, len);
  (*dest)[len]=0;
  
  while (*p=='\r' || *p=='\n' || *p=='|')
    p++;
  
  if (*p)  return p;  /* not-last text field */
  else return NULL;   /* last text field     */
}

static subtitle_t *sub_read_line_microdvd(sputext_decoder_t *this, subtitle_t *current) {

  char line[1001];
  char line2[1001];
  char *p, *next;
  int i;
  
  bzero (current, sizeof(subtitle_t));
  
  current->end=-1;
  do {
    if (!fgets (line, 1000, this->fd)) return NULL;
  } while ((sscanf (line, "{%ld}{}%[^\r\n]", &(current->start), line2) !=2) &&
           (sscanf (line, "{%ld}{%ld}%[^\r\n]", &(current->start), &(current->end),line2) !=3)
	  );
  
  p=line2;
  
  next=p, i=0;
  while ((next =sub_readtext (next, &(current->text[i])))) {
    if (current->text[i]==ERR) return ERR;
    i++;
    if (i>=SUB_MAX_TEXT) { 
      printf ("Too many lines in a subtitle\n");
      current->lines=i;
      return current;
    }
  }
  current->lines= ++i;
  
  return current;
}

static subtitle_t *sub_read_line_subrip(sputext_decoder_t *this, subtitle_t *current) {

  char line[1001];
  int a1,a2,a3,a4,b1,b2,b3,b4;
  char *p=NULL, *q=NULL;
  int len;
  
  bzero (current, sizeof(subtitle_t));
  
  while (1) {
    if (!fgets (line, 1000, this->fd)) return NULL;
    if (sscanf (line, "%d:%d:%d.%d,%d:%d:%d.%d",&a1,&a2,&a3,&a4,&b1,&b2,&b3,&b4) < 8) continue;
    current->start = a1*360000+a2*6000+a3*100+a4;
    current->end   = b1*360000+b2*6000+b3*100+b4;
    
    if (!fgets (line, 1000, this->fd)) return NULL;
    
    p=q=line;
    for (current->lines=1; current->lines < SUB_MAX_TEXT; current->lines++) {
      for (q=p,len=0; *p && *p!='\r' && *p!='\n' && strncmp(p,"[br]",4); p++,len++);
      current->text[current->lines-1]=(char *)xine_xmalloc (len+1);
      if (!current->text[current->lines-1]) return ERR;
      strncpy (current->text[current->lines-1], q, len);
      current->text[current->lines-1][len]='\0';
      if (!*p || *p=='\r' || *p=='\n') break;
      while (*p++!=']');
    }
    break;
  }
  return current;
}

static subtitle_t *sub_read_line_third(sputext_decoder_t *this,subtitle_t *current) {
  char line[1001];
  int a1,a2,a3,a4,b1,b2,b3,b4;
  char *p=NULL;
  int i,len;
  
  bzero (current, sizeof(subtitle_t));
  
  while (!current->text[0]) {
    if (!fgets (line, 1000, this->fd)) return NULL;
    if ((len=sscanf (line, "%d:%d:%d,%d --> %d:%d:%d,%d",&a1,&a2,&a3,&a4,&b1,&b2,&b3,&b4)) < 8)
      continue;
    current->start = a1*360000+a2*6000+a3*100+a4/10;
    current->end   = b1*360000+b2*6000+b3*100+b4/10;
    for (i=0; i<SUB_MAX_TEXT;) {
      if (!fgets (line, 1000, this->fd)) break;
      len=0;
      for (p=line; *p!='\n' && *p!='\r' && *p; p++,len++);
      if (len) {
	current->text[i]=(char *)xine_xmalloc (len+1);
	if (!current->text[i]) return ERR;
	strncpy (current->text[i], line, len); current->text[i][len]='\0';
	i++;
      } else {
	break;
      }
    }
    current->lines=i;
  }
  return current;
}

static subtitle_t *sub_read_line_vplayer(sputext_decoder_t *this,subtitle_t *current) {
  char line[1001];
  char line2[1001];
  int a1,a2,a3,b1,b2,b3;
  char *p=NULL, *next;
  int i,len,len2,plen;
  
  bzero (current, sizeof(subtitle_t));
  
  while (!current->text[0]) {
    if (!fgets (line, 1000, this->fd)) return NULL;
    if ((len=sscanf (line, "%d:%d:%d:%n",&a1,&a2,&a3,&plen)) < 3)
      continue;
    if (!fgets (line2, 1000, this->fd)) return NULL;
    if ((len2=sscanf (line2, "%d:%d:%d:",&b1,&b2,&b3)) < 3)
      continue;
    /* przewiñ o linijkê do ty³u: */
    fseek(this->fd,-strlen(line2),SEEK_CUR);
    
    current->start = a1*360000+a2*6000+a3*100;
    current->end   = b1*360000+b2*6000+b3*100;
    if ((current->end - current->start) > 1000) 
      current->end = current->start + 1000; /* not too long though.  */
    /* teraz czas na wkopiowanie stringu */
    p=line;	
    /* finds the body of the subtitle_t */
    for (i=0; i<3; i++){              
      p=strchr(p,':')+1;
    } 
    i=0;
    
    if (*p!='|') {
      next = p,i=0;
      while ((next =sub_readtext (next, &(current->text[i])))) {
	if (current->text[i]==ERR) 
	  return ERR;
	i++;
	if (i>=SUB_MAX_TEXT) { 
	  printf ("Too many lines in a subtitle\n");
	  current->lines=i;
	  return current;
	}
      }
      current->lines=i+1;
    }
  }
  return current;
}

static subtitle_t *sub_read_line_rt(sputext_decoder_t *this,subtitle_t *current) {
  /*
   * TODO: This format uses quite rich (sub/super)set of xhtml 
   * I couldn't check it since DTD is not included.
   * WARNING: full XML parses can be required for proper parsing 
   */
  char line[1001];
  int a1,a2,a3,a4,b1,b2,b3,b4;
  char *p=NULL,*next=NULL;
  int i,len,plen;
  
  bzero (current, sizeof(subtitle_t));
  
  while (!current->text[0]) {
    if (!fgets (line, 1000, this->fd)) return NULL;
    /*
     * TODO: it seems that format of time is not easily determined, it may be 1:12, 1:12.0 or 0:1:12.0
     * to describe the same moment in time. Maybe there are even more formats in use.
     */
    if ((len=sscanf (line, "<Time Begin=\"%d:%d:%d.%d\" End=\"%d:%d:%d.%d\"",&a1,&a2,&a3,&a4,&b1,&b2,&b3,&b4)) < 8)
     
      plen=a1=a2=a3=a4=b1=b2=b3=b4=0;
    if (
	((len=sscanf (line, "<%*[tT]ime %*[bB]egin=\"%d:%d\" %*[Ee]nd=\"%d:%d\"%*[^<]<clear/>%n",&a2,&a3,&b2,&b3,&plen)) < 4) &&
	((len=sscanf (line, "<%*[tT]ime %*[bB]egin=\"%d:%d\" %*[Ee]nd=\"%d:%d.%d\"%*[^<]<clear/>%n",&a2,&a3,&b2,&b3,&b4,&plen)) < 5) &&
	/*	((len=sscanf (line, "<%*[tT]ime %*[bB]egin=\"%d:%d.%d\" %*[Ee]nd=\"%d:%d\"%*[^<]<clear/>%n",&a2,&a3,&a4,&b2,&b3,&plen)) < 5) && */
	((len=sscanf (line, "<%*[tT]ime %*[bB]egin=\"%d:%d.%d\" %*[Ee]nd=\"%d:%d.%d\"%*[^<]<clear/>%n",&a2,&a3,&a4,&b2,&b3,&b4,&plen)) < 6) &&
	((len=sscanf (line, "<%*[tT]ime %*[bB]egin=\"%d:%d:%d.%d\" %*[Ee]nd=\"%d:%d:%d.%d\"%*[^<]<clear/>%n",&a1,&a2,&a3,&a4,&b1,&b2,&b3,&b4,&plen)) < 8) 
	)
      continue;
    current->start = a1*360000+a2*6000+a3*100+a4/10;
    current->end   = b1*360000+b2*6000+b3*100+b4/10;
    p=line;	p+=plen;i=0;
    /* TODO: I don't know what kind of convention is here for marking multiline subs, maybe <br/> like in xml? */
    next = strstr(line,"<clear/>")+8;i=0;
    while ((next =sub_readtext (next, &(current->text[i])))) {
      if (current->text[i]==ERR) 
	return ERR;
      i++;
      if (i>=SUB_MAX_TEXT) { 
	printf ("Too many lines in a subtitle\n");
	current->lines=i;
	return current;
      }
    }
    current->lines=i+1;
  }
  return current;
}

static subtitle_t *sub_read_line_ssa(sputext_decoder_t *this,subtitle_t *current) {

  int hour1, min1, sec1, hunsec1,
    hour2, min2, sec2, hunsec2, nothing;
  int num;
  
  char line[1000],
    line3[1000],
    *line2;
  char *tmp;
  
  do {
    if (!fgets (line, 1000, this->fd)) 
      return NULL;
  } while (sscanf (line, "Dialogue: Marked=%d,%d:%d:%d.%d,%d:%d:%d.%d,"
		   "%[^\n\r]", &nothing,
		   &hour1, &min1, &sec1, &hunsec1, 
		   &hour2, &min2, &sec2, &hunsec2,
		   line3) < 9);
  line2=strstr(line3,",,");
  if (!line2) return NULL;
  line2 ++;
  line2 ++;

  current->lines=1;num=0;
  current->start = 360000*hour1 + 6000*min1 + 100*sec1 + hunsec1;
  current->end   = 360000*hour2 + 6000*min2 + 100*sec2 + hunsec2;
  
  while ( (tmp=strstr(line2, "\\n")) ) {
    current->text[num]=(char *)xine_xmalloc(tmp-line2+1);
    strncpy (current->text[num], line2, tmp-line2);
    current->text[num][tmp-line2]='\0';
    line2=tmp+2;
    num++;
    current->lines++;
    if (current->lines >=  SUB_MAX_TEXT) return current;
  }
  
  
  current->text[num]=(char *) xine_xmalloc(strlen(line2)+1);
  strcpy(current->text[num],line2);
  
  return current;
}

static subtitle_t *sub_read_line_dunnowhat (sputext_decoder_t *this, subtitle_t *current) {
  char line[1001];
  char text[1001];
  
  bzero (current, sizeof(subtitle_t));
  
  if (!fgets (line, 1000, this->fd))
    return NULL;
  if (sscanf (line, "%ld,%ld,\"%[^\"]", &(current->start),
	      &(current->end), text) <3)
    return ERR;
  current->text[0] = strdup(text);
  current->lines = 1;
  
  return current;
}

static subtitle_t *sub_read_line_mpsub (sputext_decoder_t *this, subtitle_t *current) {
  char line[1000];
  float a,b;
  int num=0;
  char *p, *q;
  
  do {
    if (!fgets(line, 1000, this->fd)) 
      return NULL;
  } while (sscanf (line, "%f %f", &a, &b) !=2);

  this->mpsub_position += (a*100.0);
  current->start = (int) this->mpsub_position;
  this->mpsub_position += (b*100.0);
  current->end = (int) this->mpsub_position;
  
  while (num < SUB_MAX_TEXT) {
    if (!fgets (line, 1000, this->fd)) 
      return NULL;

    p=line;
    while (isspace(*p)) 
      p++;

    if (eol(*p) && num > 0) 
      return current;

    if (eol(*p)) 
      return NULL;
    
    for (q=p; !eol(*q); q++);
    *q='\0';
    if (strlen(p)) {
      current->text[num]=strdup(p);
      printf (">%s<\n",p);
      current->lines = ++num;
    } else {
      if (num) 
	return current;
      else 
	return NULL;
    }
  }

  return NULL;
}

static subtitle_t *sub_read_line_aqt (sputext_decoder_t *this, subtitle_t *current) {
  char line[1001];

  bzero (current, sizeof(subtitle_t));

  while (1) {
    /* try to locate next subtitle_t */
    if (!fgets (line, 1000, this->fd))
      return NULL;
    if (!(sscanf (line, "-->> %ld", &(current->start)) <1))
      break;
  }
  
  if (this->previous_aqt_sub != NULL) 
    this->previous_aqt_sub->end = current->start-1;
  
  this->previous_aqt_sub = current;
  
  if (!fgets (line, 1000, this->fd))
    return NULL;
  
  sub_readtext((char *) &line,&current->text[0]);
  current->lines = 1;
  current->end = current->start; /* will be corrected by next subtitle_t */
  
  if (!fgets (line, 1000, this->fd))
    return current;;
  
  sub_readtext((char *) &line,&current->text[1]);
  current->lines = 2;
  
  if ((current->text[0]=="") && (current->text[1]=="")) {
    /* void subtitle -> end of previous marked and exit */
    this->previous_aqt_sub = NULL;
    return NULL;
  }
  
  return current;
}

static int sub_autodetect (sputext_decoder_t *this) {

  char line[1001];
  int i,j=0;
  char p;
  
  while (j < 100) {
    j++;
    if (!fgets (line, 1000, this->fd))
      return -1;
    
    if ((sscanf (line, "{%d}{}", &i)==1) ||
        (sscanf (line, "{%d}{%d}", &i, &i)==2)) {
      this->uses_time=0;
      printf ("sputext: microdvd subtitle format detected\n");
      return FORMAT_MICRODVD;
    }

    if (sscanf (line, "%d:%d:%d.%d,%d:%d:%d.%d",     &i, &i, &i, &i, &i, &i, &i, &i)==8){
      this->uses_time=1;
      printf ("sputext: subrip subtitle format detected\n");
      return FORMAT_SUBRIP;
    }

    if (sscanf (line, "%d:%d:%d,%d --> %d:%d:%d,%d", &i, &i, &i, &i, &i, &i, &i, &i)==8) {
      this->uses_time=1;
      printf ("sputext: subviewer subtitle format detected\n");
      return FORMAT_SUBVIEWER;
    }
    if (strstr (line, "<SAMI>")) {
      this->uses_time=1; 
      printf ("sputext: sami subtitle format detected\n");
      return FORMAT_SAMI;
    }
    if (sscanf (line, "%d:%d:%d:",     &i, &i, &i )==3) {
      this->uses_time=1;
      printf ("sputext: vplayer subtitle format detected\n");
      return FORMAT_VPLAYER;
    }
    /*
     * TODO: just checking if first line of sub starts with "<" is WAY
     * too weak test for RT
     * Please someone who knows the format of RT... FIX IT!!!
     * It may conflict with other sub formats in the future (actually it doesn't)
     */
    if ( *line == '<' ) {
      this->uses_time=1;
      printf ("sputext: rt subtitle format detected\n");
      return FORMAT_RT;
    }
    if (!memcmp(line, "Dialogue: Marked", 16)){
      this->uses_time=1; 
      printf ("sputext: ssa subtitle format detected\n");
      return FORMAT_SSA;
    }
    if (sscanf (line, "%d,%d,\"%c", &i, &i, (char *) &i) == 3) {
      this->uses_time=0;
      printf ("sputext: (dunno) subtitle format detected\n");
      return FORMAT_DUNNO;
    }
    if (sscanf (line, "FORMAT=%d", &i) == 1) {
      this->uses_time=0; 
      printf ("sputext: mpsub subtitle format detected\n");
      return FORMAT_MPSUB;
    }
    if (sscanf (line, "FORMAT=TIM%c", &p)==1 && p=='E') {
      this->uses_time=1; 
      printf ("sputext: mpsub subtitle format detected\n");
      return FORMAT_MPSUB;
    }
    if (strstr (line, "-->>")) {
      this->uses_time=0; 
      printf ("sputext: aqtitle subtitle format detected\n");
      return FORMAT_AQTITLE;
    }
  }
  
  return -1;  /* too many bad lines */
}

static subtitle_t *sub_read_file (sputext_decoder_t *this) {

  int n_max;
  subtitle_t *first;
  subtitle_t * (*func[])(sputext_decoder_t *this,subtitle_t *dest)=
  {
    sub_read_line_microdvd,
    sub_read_line_subrip,
    sub_read_line_third,
    sub_read_line_sami,
    sub_read_line_vplayer,
    sub_read_line_rt,
    sub_read_line_ssa,
    sub_read_line_dunnowhat,
    sub_read_line_mpsub,
    sub_read_line_aqt
    
  };
  iconv_t iconv_descr;

  this->format=sub_autodetect (this);
  if (this->format==-1) {
    printf ("sputext: Could not determine file format\n");
    return NULL;
  }

  printf ("sputext: Detected subtitle file format: %d\n",this->format);
    
  rewind (this->fd);

  this->num=0;n_max=32;
  first = (subtitle_t *) xine_xmalloc(n_max*sizeof(subtitle_t));
  if(!first) return NULL;
    
  iconv_descr=iconv_open(this->dst_encoding,this->src_encoding);
  while(1){
    subtitle_t *sub;
    if(this->num>=n_max){
      n_max+=16;
      first=realloc(first,n_max*sizeof(subtitle_t));
    }
    sub = func[this->format] (this, &first[this->num]);
    if (!sub) 
      break;   /* EOF */

    if (sub==ERR) 
      ++this->errs; 
    else {
      int i;
      if (this->num > 0 && first[this->num-1].end == -1) {
	first[this->num-1].end = sub->start;
      }
      for(i=0; i<first[this->num].lines; i++)
      { char *tmp;
	char *in_buff, *out_buff;
	int   in_len, out_len;

	in_len=strlen(first[this->num].text[i])+1;
	tmp=malloc(in_len);
	in_buff=first[this->num].text[i];
	out_buff=tmp;
	out_len=in_len;
	if ((size_t)(-1)!=iconv(iconv_descr,&in_buff,&in_len,&out_buff,&out_len))
	{ free(first[this->num].text[i]);
	  first[this->num].text[i]=tmp;
	}
	else
        { printf("sputext: Can't convert subtitle text\n"); }
      }
      ++this->num; /* Error vs. Valid */
    }
  }
  iconv_close(iconv_descr);

  printf ("sputext: Read %i subtitles", this->num);
  if (this->errs) 
    printf (", %i bad line(s).\n", this->errs);
  else 	  
    printf (".\n");
  
  return first;
}


static void list_sub_file (sputext_decoder_t *this, subtitle_t* subs) {

  int i,j;

  for(j=0;j<this->num;j++){
    subtitle_t* egysub=&subs[j];
    printf ("%i line%c (%li-%li) ",
	    egysub->lines,
	    (1==egysub->lines)?' ':'s',
	    egysub->start,
	    egysub->end);
    for (i=0; i<egysub->lines; i++) {
      printf ("%s%s",egysub->text[i], i==egysub->lines-1?"":" <BREAK> ");
    }
    printf ("\n");
  }
  
  printf ("Subtitle format %s time.\n", this->uses_time?"uses":"doesn't use");
  printf ("Read %i subtitles, %i errors.\n", this->num, this->errs);
  
}

static int spudec_can_handle (spu_decoder_t *this_gen, int buf_type) {
  int type = buf_type & 0xFFFF0000;
  return (type == BUF_SPU_TEXT); 
}


static void spudec_init (spu_decoder_t *this_gen, vo_instance_t *vo_out) {

  sputext_decoder_t *this = (sputext_decoder_t *) this_gen;

  this->vo_out             = vo_out;
  this->output_open        = 0;

  this->mpsub_position     = 0;
  this->uses_time          = 0;
  this->errs               = 0;
  this->num                = 0;
  this->format             = 0;
  this->previous_aqt_sub   = NULL;

}

static void update_font_size (sputext_decoder_t *this) {
  static int sizes[SUBTITLE_SIZE_NUM][4] = {
    { 16, 16, 16, 20 }, /* SUBTITLE_SIZE_SMALL  */
    { 16, 16, 20, 24 }, /* SUBTITLE_SIZE_NORMAL */
    { 16, 20, 24, 32 }, /* SUBTITLE_SIZE_LARGE  */
  };

  int *vec = sizes[this->subtitle_size];
  int y;

  if( this->width >= 512 )
    this->font_size = vec[3];
  else if( this->width >= 384 )
    this->font_size = vec[2];
  else if( this->width >= 320 )
    this->font_size = vec[1];
  else
    this->font_size = vec[0];
  
  this->line_height = this->font_size + 10;

  y = this->height - (SUB_MAX_TEXT * this->line_height) - 5;
  
  if( this->osd )
    this->renderer->free_object (this->osd);

  if(this->renderer) {
    this->osd = this->renderer->new_object (this->renderer, 
					    this->width,
					    SUB_MAX_TEXT * this->line_height);
    
    this->renderer->set_font (this->osd, this->font, this->font_size);
    this->renderer->set_position (this->osd, 0, y);
  }
}

static void spudec_decode_data (spu_decoder_t *this_gen, buf_element_t *buf) {

  sputext_decoder_t *this = (sputext_decoder_t *) this_gen;
  int64_t current_time;
  
  if (buf->decoder_flags & BUF_FLAG_HEADER) {
    this->width  = buf->decoder_info[1];
    this->height = buf->decoder_info[2];

    this->renderer = this->xine->osd_renderer;
    this->osd = NULL;

    update_font_size (this);
    
    current_time = this->xine->metronom->get_current_time (this->xine->metronom);
    this->renderer->show (this->osd, current_time);
    this->renderer->hide (this->osd, current_time+300000);      

    this->fd = (FILE *) buf->content;
  
    this->subtitles = sub_read_file (this);

    printf ("sputext: subtitle format %s time.\n", this->uses_time?"uses":"doesn't use");
    printf ("sputext: read %i subtitles, %i errors.\n", this->num, this->errs);

    this->cur = 0;

  } else {

    int64_t     sub_start, sub_end;
    subtitle_t *subtitle;

    /* don't want to see subtitle */
    if(this->xine->spu_channel_user == -2)
      return;

    subtitle = NULL;
    
    /* 
     * find out which subtitle to display 
     */

    if (!this->uses_time) {
      int         frame_num;
      int64_t     pts_factor;
      long        frame_offset;

      frame_num = buf->decoder_info[1];

      /* FIXME FIXME FIXME 
      pts_factor = this->xine->metronom->get_video_rate (this->xine->metronom);
      */
      pts_factor = 3000;

      frame_offset = this->time_offset * 900 / pts_factor;

      while ( (this->cur < this->num) 
	      && (this->subtitles[this->cur].start + frame_offset < frame_num) )
	this->cur++;

      if (this->cur >= this->num)
	return;

      subtitle = &this->subtitles[this->cur];

      if (subtitle->start + frame_offset > frame_num)
	return;

      sub_start = this->xine->metronom->got_spu_packet(this->xine->metronom, buf->pts);
      sub_end = sub_start + (subtitle->end - subtitle->start) * pts_factor;

    } else {
      uint32_t start_tenth;

      start_tenth = buf->pts/900;

#ifdef LOG
      printf ("sputext: searching for spu for %d\n", start_tenth);
#endif

      while ( (this->cur < this->num) 
	      && (this->subtitles[this->cur].start + this->time_offset < start_tenth) )
	this->cur++;

      if (this->cur >= this->num)
	return;

#ifdef LOG
      printf ("sputext: found >%s<, start %ld, end %ld\n", this->subtitles[this->cur].text[0],
	      this->subtitles[this->cur].start + this->time_offset, this->subtitles[this->cur].end);
#endif

      subtitle = &this->subtitles[this->cur];

      if (subtitle->start + this->time_offset > (start_tenth+20))
	return;

      sub_start = this->xine->metronom->got_spu_packet(this->xine->metronom, (subtitle->start+this->time_offset)*900);
      sub_end = sub_start + (subtitle->end - subtitle->start)*900;
    }

    if( !sub_start )
      return;

    if (subtitle) {
      int line, y;
      int font_size;

      this->renderer->filled_rect (this->osd, 0, 0, this->width-1, this->line_height * SUB_MAX_TEXT - 1, 0);

      y = (SUB_MAX_TEXT - subtitle->lines) * this->line_height;
      font_size = this->font_size;
      
      for (line=0; line<subtitle->lines; line++) {
        int w,h,x;
        
        while(1) {
          this->renderer->get_text_size( this->osd, subtitle->text[line], 
                                         &w, &h);
          x = (this->width - w) / 2;
          
          if( w > this->width && font_size > 16 ) {
            font_size -= 4;
            this->renderer->set_font (this->osd, this->font, font_size);
          } else {
            break;
          }
        }
        
        this->renderer->render_text (this->osd, x, y + line*this->line_height, subtitle->text[line], OSD_TEXT1);
      }
       
      if( font_size != this->font_size )
        this->renderer->set_font (this->osd, this->font, this->font_size);

      if( this->last_subtitle_end && sub_start < this->last_subtitle_end ) {
	sub_start = this->last_subtitle_end;
      }
      this->last_subtitle_end = sub_end;
        
      this->renderer->set_text_palette (this->osd, -1, OSD_TEXT1);
      this->renderer->show (this->osd, sub_start);
      this->renderer->hide (this->osd, sub_end);

#ifdef LOG
      printf ("sputext: scheduling subtitle >%s< at %lld until %lld, current time is %lld\n",
	      subtitle->text[0], sub_start, sub_end, 
	      this->xine->metronom->get_current_time (this->xine->metronom));
#endif

    }

    this->cur++;
  }
}  


static void spudec_reset (spu_decoder_t *this_gen) {
  sputext_decoder_t *this = (sputext_decoder_t *) this_gen;

  this->cur = 0;
}

static void spudec_close (spu_decoder_t *this_gen) {
  sputext_decoder_t *this = (sputext_decoder_t *) this_gen;

  if (this->osd) {
    this->renderer->free_object (this->osd);
    this->osd = NULL;
  }

}

static char *spudec_get_id(void) {
  return "sputext";
}

static void update_osd_font(void *this_gen, xine_cfg_entry_t *entry)
{
  sputext_decoder_t *this = (sputext_decoder_t *)this_gen;

  this->font = entry->str_value;
  
  if( this->renderer )
    this->renderer->set_font (this->osd, this->font, this->font_size);
  
  printf("libsputext: spu_font = %s\n", this->font );
}

static void update_osd_src_encoding(void *this_gen, xine_cfg_entry_t *entry)
{
  sputext_decoder_t *this = (sputext_decoder_t *)this_gen;

  this->src_encoding = entry->str_value;
  
  printf("libsputext: spu_src_encoding = %s\n", this->src_encoding );
}

static void update_osd_dst_encoding(void *this_gen, xine_cfg_entry_t *entry)
{
  sputext_decoder_t *this = (sputext_decoder_t *)this_gen;

  this->dst_encoding = entry->str_value;
  
  printf("libsputext: spu_dst_encoding = %s\n", this->dst_encoding );
}

static void update_subtitle_size(void *this_gen, xine_cfg_entry_t *entry)
{
  sputext_decoder_t *this = (sputext_decoder_t *)this_gen;

  this->subtitle_size = entry->num_value;

  update_font_size (this_gen);
}

static void update_time_offset(void *this_gen, xine_cfg_entry_t *entry)
{
  sputext_decoder_t *this = (sputext_decoder_t *)this_gen;

  this->time_offset = entry->num_value;

  printf("libsputext: time_offset = %d\n", this->time_offset );
}

static void spudec_dispose (spu_decoder_t *this_gen) {
  free (this_gen);
}

spu_decoder_t *init_spu_decoder_plugin (int iface_version, xine_t *xine) {

  sputext_decoder_t *this ;
  static char *subtitle_size_strings[] = { "small", "normal", "large", NULL };

  if (iface_version != 9) {
    printf(_("libsputext: doesn't support plugin api version %d.\n"
	     "libsputext: This means there is a version mismatch between xine and\n"
	     "libsputext: this plugin.\n"), iface_version);
    return NULL;
  }

  this = (sputext_decoder_t *) xine_xmalloc (sizeof (sputext_decoder_t));

  this->spu_decoder.interface_version   = iface_version;
  this->spu_decoder.can_handle          = spudec_can_handle;
  this->spu_decoder.init                = spudec_init;
  this->spu_decoder.decode_data         = spudec_decode_data;
  this->spu_decoder.reset               = spudec_reset;
  this->spu_decoder.close               = spudec_close;
  this->spu_decoder.get_identifier      = spudec_get_id;
  this->spu_decoder.dispose             = spudec_dispose;
  this->spu_decoder.priority            = 1;

  this->xine                            = xine;
  this->font                            = xine->config->register_string(xine->config, 
									"codec.spu_font", 
									"sans", 
									_("font for avi subtitles"), 
									NULL, 0, update_osd_font, this);
  this->subtitle_size                    = xine->config->register_enum(xine->config, 
								       "codec.spu_subtitle_size", 
								       1,
								       subtitle_size_strings,
								       _("subtitle size (relative window size)"), 
								       NULL, 0, update_subtitle_size, this);
  this->src_encoding                    = xine->config->register_string(xine->config, 
									"codec.spu_src_encoding", 
									"windows-1250", 
									_("source encoding of subtitles"), 
									NULL, 10, update_osd_src_encoding, this);
  this->dst_encoding                    = xine->config->register_string(xine->config, 
									"codec.spu_dst_encoding", 
									"iso-8859-2", 
									_("target encoding for subtitles (have to match font encoding)"), 
									NULL, 10, update_osd_dst_encoding, this);
  this->time_offset                     = xine->config->register_num   (xine->config, 
								        "codec.spu_time_offset", 
								        0,
								        _("subtitle time offset in 1/100 sec"), 
								        NULL, 10, update_time_offset, this);

  return (spu_decoder_t *) this;
}

