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
 * $Id: demux_sputext.c,v 1.14 2003/03/26 18:51:55 miguelfreitas Exp $
 *
 * code based on old libsputext/xine_decoder.c
 *
 * code based on mplayer module:
 *
 * Subtitle reader with format autodetection
 *
 * Written by laaz
 * Some code cleanup & realloc() by A'rpi/ESP-team
 * dunnowhat sub format by szabi
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <ctype.h>

#include "xine_internal.h"
#include "xineutils.h"
#include "../demuxers/demux.h"

/*
#define LOG 1
*/

#define ERR (void *)-1
#define SUB_MAX_TEXT  5
#define SUB_BUFSIZE 1024

/*
 *  Demuxer typedefs
 */


typedef struct {

  int lines;

  unsigned long start; /* csecs */
  unsigned long end;   /* csecs */
    
  char *text[SUB_MAX_TEXT];

} subtitle_t;


typedef struct {

  demux_plugin_t     demux_plugin;
  xine_stream_t     *stream;
  input_plugin_t    *input;

  int                status;

  char               buf[SUB_BUFSIZE];
  off_t              buflen;

  float              mpsub_position;  

  int                uses_time;  
  int                errs;  
  subtitle_t        *subtitles;
  int                num;            /* number of subtitle structs */
  int                cur;            /* current subtitle           */
  int                format;         /* constants see below        */     
  subtitle_t        *previous_aqt_sub ;
  char               next_line[SUB_BUFSIZE]; /* a buffer for next line read from file */

} demux_sputext_t;

typedef struct demux_sputext_class_s {

  demux_class_t      demux_class;

  xine_t            *xine;
  config_values_t   *config;

  char              *src_encoding;
  char              *dst_encoding;

} demux_sputext_class_t;

/*
 * Demuxer code start
 */

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

/*
 * Reimplementation of fgets() using the input->read() method.
 */
static char *read_line_from_input(demux_sputext_t *this, char *line, off_t len) {
  off_t nread = 0;
  char *s;
  int linelen;

  if ((len - this->buflen) >512)
    nread = this->input->read(this->input, &this->buf[this->buflen], len - this->buflen);

  this->buflen += nread;
  this->buf[this->buflen] = '\0';

  s = strstr(this->buf, "\n");
  if (line && (s || this->buflen)) {

    linelen = s ? (s - this->buf) + 1 : this->buflen;
    
    memcpy(line, this->buf, linelen);
    line[linelen] = '\0';

    memmove(this->buf, &this->buf[linelen], SUB_BUFSIZE - linelen);
    this->buflen -= linelen;

    return line;
  }

  return NULL;
}


static subtitle_t *sub_read_line_sami(demux_sputext_t *this, subtitle_t *current) {

  static char line[1001];
  static char *s = NULL;
  char text[1000], *p, *q;
  int state;

  p = NULL;
  current->lines = current->start = current->end = 0;
  state = 0;
  
  /* read the first line */
  if (!s)
    if (!(s = read_line_from_input(this, line, 1000))) return 0;
  
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
    if (state != 99 && !(s = read_line_from_input (this, line, 1000))) 
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

static subtitle_t *sub_read_line_microdvd(demux_sputext_t *this, subtitle_t *current) {

  char line[1001];
  char line2[1001];
  char *p, *next;
  int i;
  
  bzero (current, sizeof(subtitle_t));
  
  current->end=-1;
  do {
    if (!read_line_from_input (this, line, 1000)) return NULL;
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

static subtitle_t *sub_read_line_subrip(demux_sputext_t *this, subtitle_t *current) {

  char line[1001];
  int a1,a2,a3,a4,b1,b2,b3,b4;
  char *p=NULL, *q=NULL;
  int len;
  
  bzero (current, sizeof(subtitle_t));
  
  while (1) {
    if (!read_line_from_input(this, line, 1000)) return NULL;
    if (sscanf (line, "%d:%d:%d.%d,%d:%d:%d.%d",&a1,&a2,&a3,&a4,&b1,&b2,&b3,&b4) < 8) continue;
    current->start = a1*360000+a2*6000+a3*100+a4;
    current->end   = b1*360000+b2*6000+b3*100+b4;
    
    if (!read_line_from_input(this, line, 1000)) return NULL;
    
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

static subtitle_t *sub_read_line_third(demux_sputext_t *this,subtitle_t *current) {
  char line[1001];
  int a1,a2,a3,a4,b1,b2,b3,b4;
  char *p=NULL;
  int i,len;
  
  bzero (current, sizeof(subtitle_t));
  
  while (!current->text[0]) {
    if (!read_line_from_input(this, line, 1000)) return NULL;
  
    if ((len=sscanf (line, "%d:%d:%d,%d --> %d:%d:%d,%d",&a1,&a2,&a3,&a4,&b1,&b2,&b3,&b4)) < 8)
      continue;
    current->start = a1*360000+a2*6000+a3*100+a4/10;
    current->end   = b1*360000+b2*6000+b3*100+b4/10;
    for (i=0; i<SUB_MAX_TEXT;) {
      if (!read_line_from_input(this, line, 1000)) break;
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

static subtitle_t *sub_read_line_vplayer(demux_sputext_t *this,subtitle_t *current) {
  char line[1001];
  int a1,a2,a3,b1,b2,b3;
  char *p=NULL, *next, *p2;
  int i;
  
  bzero (current, sizeof(subtitle_t));
    
  while (!current->text[0]) {
    if( this->next_line[0] == '\0' ) { /* if the buffer is empty.... */
      if( !read_line_from_input(this, line, 1000) ) return NULL;
    } else {
      /* ... get the current line from buffer. */
      strncpy( line, this->next_line, 1000);
      line[1000] = '\0'; /* I'm scared. This makes me feel better. */
      this->next_line[0] = '\0'; /* mark the buffer as empty. */
    }
    /* Initialize buffer with next line */
    if( ! read_line_from_input( this, this->next_line, 1000) ) {
      this->next_line[0] = '\0';
      return NULL;
    }
    if( (sscanf( line,            "%d:%d:%d:", &a1, &a2, &a3) < 3) ||
        (sscanf( this->next_line, "%d:%d:%d:", &b1, &b2, &b3) < 3) )
      continue;
    current->start = a1*360000+a2*6000+a3*100;
    current->end   = b1*360000+b2*6000+b3*100;
    if ((current->end - current->start) > 1000) 
      current->end = current->start + 1000; /* not too long though.  */
    /* teraz czas na wkopiowanie stringu */
    p=line;
    /* finds the body of the subtitle_t */
    for (i=0; i<3; i++){              
      p2=strchr( p, ':');
      if( p2 == NULL ) break;
      p=p2+1;
    } 
      
    next=p;
    i=0;
    while( (next = sub_readtext( next, &(current->text[i]))) ) {
      if (current->text[i]==ERR) 
        return ERR;
      i++;
      if (i>=SUB_MAX_TEXT) { 
        printf ("Too many lines in a subtitle\n");
        current->lines=i;
        return current;
      }
    }
    current->lines=++i;
  }
  return current;
}

static subtitle_t *sub_read_line_rt(demux_sputext_t *this,subtitle_t *current) {
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
    if (!read_line_from_input(this, line, 1000)) return NULL;
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

static subtitle_t *sub_read_line_ssa(demux_sputext_t *this,subtitle_t *current) {

  int hour1, min1, sec1, hunsec1,
    hour2, min2, sec2, hunsec2, nothing;
  int num;
  
  char line[1000],
    line3[1000],
    *line2;
  char *tmp;
  
  do {
    if (!read_line_from_input(this, line, 1000)) 
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

static subtitle_t *sub_read_line_dunnowhat (demux_sputext_t *this, subtitle_t *current) {
  char line[1001];
  char text[1001];
  
  bzero (current, sizeof(subtitle_t));
  
  if (!read_line_from_input(this, line, 1000))
    return NULL;
  if (sscanf (line, "%ld,%ld,\"%[^\"]", &(current->start),
	      &(current->end), text) <3)
    return ERR;
  current->text[0] = strdup(text);
  current->lines = 1;
  
  return current;
}

static subtitle_t *sub_read_line_mpsub (demux_sputext_t *this, subtitle_t *current) {
  char line[1000];
  float a,b;
  int num=0;
  char *p, *q;
  
  do {
    if (!read_line_from_input(this, line, 1000)) 
      return NULL;
  } while (sscanf (line, "%f %f", &a, &b) !=2);

  this->mpsub_position += (a*100.0);
  current->start = (int) this->mpsub_position;
  this->mpsub_position += (b*100.0);
  current->end = (int) this->mpsub_position;
  
  while (num < SUB_MAX_TEXT) {
    if (!read_line_from_input(this, line, 1000)) 
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

static subtitle_t *sub_read_line_aqt (demux_sputext_t *this, subtitle_t *current) {
  char line[1001];

  bzero (current, sizeof(subtitle_t));

  while (1) {
    /* try to locate next subtitle_t */
    if (!read_line_from_input(this, line, 1000))
      return NULL;
    if (!(sscanf (line, "-->> %ld", &(current->start)) <1))
      break;
  }
  
  if (this->previous_aqt_sub != NULL) 
    this->previous_aqt_sub->end = current->start-1;
  
  this->previous_aqt_sub = current;
  
  if (!read_line_from_input(this, line, 1000))
    return NULL;
  
  sub_readtext((char *) &line,&current->text[0]);
  current->lines = 1;
  current->end = current->start; /* will be corrected by next subtitle_t */
  
  if (!read_line_from_input(this, line, 1000))
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

static int sub_autodetect (demux_sputext_t *this) {

  char line[1001];
  int i,j=0;
  char p;
  
  while (j < 100) {
    j++;
    if (!read_line_from_input(this, line, 1000))
      return -1;
    
    if ((sscanf (line, "{%d}{}", &i)==1) ||
        (sscanf (line, "{%d}{%d}", &i, &i)==2)) {
      this->uses_time=0;
      printf ("demux_sputext: microdvd subtitle format detected\n");
      return FORMAT_MICRODVD;
    }

    if (sscanf (line, "%d:%d:%d.%d,%d:%d:%d.%d",     &i, &i, &i, &i, &i, &i, &i, &i)==8){
      this->uses_time=1;
      printf ("demux_sputext: subrip subtitle format detected\n");
      return FORMAT_SUBRIP;
    }

    if (sscanf (line, "%d:%d:%d,%d --> %d:%d:%d,%d", &i, &i, &i, &i, &i, &i, &i, &i)==8) {
      this->uses_time=1;
      printf ("demux_sputext: subviewer subtitle format detected\n");
      return FORMAT_SUBVIEWER;
    }
    if (strstr (line, "<SAMI>")) {
      this->uses_time=1; 
      printf ("demux_sputext: sami subtitle format detected\n");
      return FORMAT_SAMI;
    }
    if (sscanf (line, "%d:%d:%d:",     &i, &i, &i )==3) {
      this->uses_time=1;
      printf ("demux_sputext: vplayer subtitle format detected\n");
      return FORMAT_VPLAYER;
    }
    /*
     * A RealText format is a markup language, starts with <window> tag,
     * options (behaviour modifiers) are possible.
     */
    if ( !strcasecmp(line, "<window") ) {
      this->uses_time=1;
      printf ("demux_sputext: rt subtitle format detected\n");
      return FORMAT_RT;
    }
    if (!memcmp(line, "Dialogue: Marked", 16)){
      this->uses_time=1; 
      printf ("demux_sputext: ssa subtitle format detected\n");
      return FORMAT_SSA;
    }
    if (sscanf (line, "%d,%d,\"%c", &i, &i, (char *) &i) == 3) {
      this->uses_time=0;
      printf ("demux_sputext: (dunno) subtitle format detected\n");
      return FORMAT_DUNNO;
    }
    if (sscanf (line, "FORMAT=%d", &i) == 1) {
      this->uses_time=0; 
      printf ("demux_sputext: mpsub subtitle format detected\n");
      return FORMAT_MPSUB;
    }
    if (sscanf (line, "FORMAT=TIM%c", &p)==1 && p=='E') {
      this->uses_time=1; 
      printf ("demux_sputext: mpsub subtitle format detected\n");
      return FORMAT_MPSUB;
    }
    if (strstr (line, "-->>")) {
      this->uses_time=0; 
      printf ("demux_sputext: aqtitle subtitle format detected\n");
      return FORMAT_AQTITLE;
    }
  }
  
  return -1;  /* too many bad lines */
}

static subtitle_t *sub_read_file (demux_sputext_t *this) {

  int n_max;
  subtitle_t *first;
  subtitle_t * (*func[])(demux_sputext_t *this,subtitle_t *dest)=
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

  this->format=sub_autodetect (this);
  if (this->format==-1) {
    printf ("demux_sputext: Could not determine file format\n");
    return NULL;
  }

  printf ("demux_sputext: Detected subtitle file format: %d\n",this->format);
    
  /* Rewind */
  this->input->seek(this->input, 0, SEEK_SET);
  this->buflen = 0;

  this->num=0;n_max=32;
  first = (subtitle_t *) xine_xmalloc(n_max*sizeof(subtitle_t));
  if(!first) return NULL;
    
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
      if (this->num > 0 && first[this->num-1].end == -1) {
	first[this->num-1].end = sub->start;
      }
      ++this->num; /* Error vs. Valid */
    }
  }

  printf ("demux_sputext: Read %i subtitles", this->num);
  if (this->errs) 
    printf (", %i bad line(s).\n", this->errs);
  else 	  
    printf (".\n");
  
  return first;
}

static int demux_sputext_next (demux_sputext_t *this_gen) {
  demux_sputext_t *this = (demux_sputext_t *) this_gen;
  buf_element_t *buf;
  uint32_t *val;
  char *str;
  subtitle_t *sub;
  int line;
  
  if (this->cur >= this->num)
    return 0;

  sub = &this->subtitles[this->cur];
  
  buf = this->stream->video_fifo->buffer_pool_alloc(this->stream->video_fifo);
  buf->type = BUF_SPU_TEXT;
  buf->pts = 0;

  val = (uint32_t * )buf->content;
  *val++ = sub->lines;
  *val++ = this->uses_time;
  *val++ = (this->uses_time) ? sub->start * 10 : sub->start;
  *val++ = (this->uses_time) ? sub->end * 10 : sub->end;
  str = (char *)val;
  for (line = 0; line < sub->lines; line++, str+=strlen(str)+1) {
    if( strlen(sub->text[line]) > SUB_BUFSIZE )
      sub->text[line][SUB_BUFSIZE] = '\0';
    strcpy(str, sub->text[line]);
  }
  
  this->stream->video_fifo->put(this->stream->video_fifo, buf);
  this->cur++;
  
  return 1;
}

static void demux_sputext_dispose (demux_plugin_t *this_gen) {
  demux_sputext_t *this = (demux_sputext_t *) this_gen;
  int i, l;
  
  for (i = 0; i < this->num; i++) {
    for (l = 0; l < this->subtitles[i].lines; l++)
      free(this->subtitles[i].text[l]);
  }
  free(this->subtitles);
  free(this);
}

static int demux_sputext_get_status (demux_plugin_t *this_gen) {
  demux_sputext_t *this = (demux_sputext_t *) this_gen;
  return this->status;
}

static int demux_sputext_get_stream_length (demux_plugin_t *this_gen) {
  demux_sputext_t   *this = (demux_sputext_t *) this_gen;

  if( this->uses_time && this->num ) {
    return this->subtitles[this->num-1].end * 10;
  } else {
    return 0;
  }
}

static int demux_sputext_send_chunk (demux_plugin_t *this_gen) {
  demux_sputext_t   *this = (demux_sputext_t *) this_gen;
  
  if (!demux_sputext_next (this)) {
    this->status = DEMUX_FINISHED;
  }

  return this->status;
}

static int demux_sputext_seek (demux_plugin_t *this_gen,
                            off_t start_pos, int start_time) {
  demux_sputext_t *this = (demux_sputext_t*)this_gen;

#ifdef LOG
  printf("demux_sputext: seek() called\n");
#endif
  /* simple seeking approach: just go back to start. 
   * decoder will discard subtitles until the desired position.
   */
  this->cur = 0;
  this->status = DEMUX_OK;
  
  xine_demux_flush_engine (this->stream);
  xine_demux_control_newpts(this->stream, 0, 0);
  
  return this->status;
}

static void demux_sputext_send_headers(demux_plugin_t *this_gen) {
  demux_sputext_t *this = (demux_sputext_t*)this_gen;
  
#ifdef LOG
  printf("demux_sputext: send_headers() called\n");
#endif
  
  xine_demux_control_start(this->stream);
  this->stream->stream_info[XINE_STREAM_INFO_HAS_VIDEO] = 0;
  this->stream->stream_info[XINE_STREAM_INFO_HAS_AUDIO] = 0;
  this->status = DEMUX_OK;
}

static uint32_t demux_sputext_get_capabilities(demux_plugin_t *this_gen) {
  return DEMUX_CAP_NOCAP;
}

static demux_plugin_t *open_demux_plugin (demux_class_t *class_gen, xine_stream_t *stream,
                                    input_plugin_t *input_gen) {

  input_plugin_t        *input = (input_plugin_t *) input_gen;
  demux_sputext_t       *this;

#ifdef LOG
  printf("demux_sputext: open_plugin() called\n");
#endif
  
  this = xine_xmalloc (sizeof (demux_sputext_t));
  this->stream = stream;
  this->input  = input;

  this->demux_plugin.send_headers      = demux_sputext_send_headers;
  this->demux_plugin.send_chunk        = demux_sputext_send_chunk;
  this->demux_plugin.seek              = demux_sputext_seek;
  this->demux_plugin.dispose           = demux_sputext_dispose;
  this->demux_plugin.get_status        = demux_sputext_get_status;
  this->demux_plugin.get_stream_length = demux_sputext_get_stream_length;
  this->demux_plugin.get_video_frame   = NULL;
  this->demux_plugin.got_video_frame_cb= NULL;
  this->demux_plugin.get_capabilities  = demux_sputext_get_capabilities;
  this->demux_plugin.get_optional_data = NULL;
  this->demux_plugin.demux_class       = class_gen;

  this->buflen = 0;

  switch (stream->content_detection_method) {
  case METHOD_BY_EXTENSION:
    {
      char *mrl, *ending;
      
      mrl = input->get_mrl(input);
      ending = strrchr(mrl, '.');

      if (!ending || (
	  (strncasecmp(ending, ".asc", 4) != 0) &&
	  (strncasecmp(ending, ".txt", 4) != 0) &&
	  (strncasecmp(ending, ".sub", 4) != 0) &&
	  (strncasecmp(ending, ".srt", 4) != 0))) {
        free (this);
        return NULL;
      }
    }
    /* falling through is intended */
    
  case METHOD_EXPLICIT:
  /* case METHOD_BY_CONTENT: */
  
    /* FIXME: for now this demuxer only works when requested explicitly
     * to make sure it does not interfere with others;
     * If this is found too inconvenient, this may be changed after making
     * sure the content detection does not produce any false positives.
     */
    
    if ((input->get_capabilities(input) & INPUT_CAP_SEEKABLE) != 0) {
      
      this->subtitles = sub_read_file (this);

      this->cur = 0;

      if (this->subtitles) {
        printf ("demux_sputext: subtitle format %s time.\n", this->uses_time?"uses":"doesn't use");
        printf ("demux_sputext: read %i subtitles, %i errors.\n", this->num, this->errs);
        return &this->demux_plugin;
      }
    }
    /* falling through is intended */
  }

  free (this);
  return NULL;
}
  
static char *get_demux_description (demux_class_t *this_gen) {
  return "sputext demuxer plugin";
}

static char *get_demux_identifier (demux_class_t *this_gen) {
  return "sputext";
}

static char *get_demux_extensions (demux_class_t *this_gen) {
  return "asc txt sub srt";
}

static char *get_demux_mimetypes (demux_class_t *this_gen) {
  return NULL;

  /* do not report this mimetype, it might confuse browsers. */
  /* "text/plain: asc txt sub srt: VIDEO subtitles;" */
}

static void demux_class_dispose (demux_class_t *this_gen) {
  demux_sputext_class_t *this = (demux_sputext_class_t *) this_gen;

  free (this);
}

static void *init_sputext_demux_class (xine_t *xine, void *data) {

  demux_sputext_class_t *this ;

#ifdef LOG
  printf("demux_sputext: initializing\n");
#endif
  
  this = xine_xmalloc (sizeof (demux_sputext_class_t));
  this->config = xine->config;

  this->demux_class.open_plugin     = open_demux_plugin;
  this->demux_class.get_description = get_demux_description;
  this->demux_class.get_identifier  = get_demux_identifier;
  this->demux_class.get_mimetypes   = get_demux_mimetypes;
  this->demux_class.get_extensions  = get_demux_extensions;
  this->demux_class.dispose         = demux_class_dispose;

  return this;
}

plugin_info_t xine_plugin_info[] = {
  /* type, API, "name", version, special_info, init_function */  
  { PLUGIN_DEMUX, 20, "sputext", XINE_VERSION_CODE, NULL, &init_sputext_demux_class },
  { PLUGIN_NONE, 0, "", 0, NULL, NULL }
};
