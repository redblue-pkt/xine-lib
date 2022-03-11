/*
 * Copyright (C) 2000-2021 the xine project
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
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110, USA
 *
 * Rip Input Plugin for catching streams
 *
 * It saves raw data into file as go from input plugins.
 *
 * Usage:
 *
 * - activation:
 *     xine stream_mrl#save:file.raw
 *
 * - it's possible speeder saving streams in the xine without playing:
 *     xine stream_mrl#save:file.raw\;noaudio\;novideo
 */

/* TODO:
 *   - resume feature (via #append)
 *   - gui activation (after restarting playback)
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#ifdef HAVE_LIBGEN_H
#include <libgen.h>
#endif

#include <stdio.h>
#include <string.h>
#include <errno.h>

#define LOG_MODULE "input_rip"
#define LOG_VERBOSE
/*
#define LOG
*/

#ifdef WIN32
#  define CLR_FAIL ""
#  define CLR_RST ""
#else
#  define CLR_FAIL "\x1b[1;31m"
#  define CLR_RST "\x1b[0;39m"
#endif

#include <xine/xine_internal.h>
#include <xine/input_plugin.h>
#include <xine/mfrag.h>
#include "xine_private.h"

#ifndef HAVE_FSEEKO
#  define fseeko fseek
#endif

#define SCRATCH_SIZE 1024
#define MAX_TARGET_LEN 512
#define SEEK_TIMEOUT 2.5

typedef struct rip_input_plugin_s {
  input_plugin_t    input_plugin;      /* inherited structure */

  input_plugin_t   *main_input_plugin; /* original input plugin */
  xine_mfrag_list_t *fraglist;         /* bypass main input for backseek */

  xine_stream_t    *stream;
  char             *fname;
  FILE             *file;              /* destination file */
  FILE             *rfile;             /* avoid lots of seeking if possible */

  char             *preview;           /* preview data */
  size_t            preview_size;      /* size of read preview data */
  off_t             curpos;            /* current position */
  off_t             savepos;           /* amount of already saved data */
  off_t             endpos;            /* skip useless "recording done" msgs */

  ssize_t         (*read) (struct rip_input_plugin_s *this, char *buf, size_t len);
  int               regular;           /* permit reading from the file */
  int               behind;            /* 0 (off), 1 (with), 2 (without read ahead) */
} rip_input_plugin_t;

/* read from main file */
static ssize_t rip_read_file_read_1a (rip_input_plugin_t *this, char *buf, size_t len) {
  size_t r = fread (buf, 1, len, this->file);
  this->curpos += r;
  if (this->curpos == this->savepos) {
    this->behind = 0;
    /* yes this is set_already, but we like to enable shared code optimization. */
    this->read = rip_read_file_read_1a;
    xprintf (this->stream->xine, XINE_VERBOSITY_DEBUG, "input_rip: live again.\n");
  }
  if (r != len) {
    int e = errno;
    xine_log (this->stream->xine, XINE_LOG_MSG,
      _("input_rip: reading of saved data failed: %s\n"), strerror (e));
  }
  return r;
}

/* read from main file, and reseek */
static ssize_t rip_read_file_read_1b (rip_input_plugin_t *this, char *buf, size_t len) {
  size_t r = 0;
  if (!fseeko (this->file, this->curpos, SEEK_SET)) {
    r = fread (buf, 1, len, this->file);
    this->curpos += r;
  }
  fseeko (this->file, this->savepos, SEEK_SET);
  if (this->curpos == this->savepos) {
    this->behind = 0;
    this->read = rip_read_file_read_1a;
    xprintf (this->stream->xine, XINE_VERBOSITY_DEBUG, "input_rip: live again.\n");
  }
  if (r != len) {
    int e = errno;
    xine_log (this->stream->xine, XINE_LOG_MSG,
      _("input_rip: reading of saved data failed: %s\n"), strerror (e));
  }
  return r;
}

/* read from clone */
static ssize_t rip_read_file_read_2 (rip_input_plugin_t *this, char *buf, size_t len) {
  size_t r = fread (buf, 1, len, this->rfile);
  if (r < len) {
    fflush (this->file);
    r = fread (buf, 1, len, this->rfile);
  }
  this->curpos += r;
  if (this->curpos == this->savepos) {
    fclose (this->rfile);
    this->rfile = NULL;
    this->behind = 0;
    this->read = rip_read_file_read_1a;
    xprintf (this->stream->xine, XINE_VERBOSITY_DEBUG, "input_rip: live again.\n");
  }
  if (r != len) {
    int e = errno;
    xine_log (this->stream->xine, XINE_LOG_MSG,
      _("input_rip: reading of saved data failed: %s\n"), strerror (e));
  }
  return r;
}

static int rip_read_file_start (rip_input_plugin_t *this, off_t offs1) {
  int mode = (this->main_input_plugin->get_capabilities (this->main_input_plugin) & INPUT_CAP_LIVE) ? 2 : 1;
  off_t offs2 = offs1 < (off_t)this->preview_size ? (off_t)this->preview_size : offs1;
  if (mode == 1) {
    if (!this->rfile && this->fname) {
      fflush (this->file);
      this->rfile = fopen (this->fname, "rb");
    }
    if (this->rfile) {
      if (fseeko (this->rfile, offs2, SEEK_SET)) {
        fclose (this->rfile);
        this->rfile = NULL;
      }
    }
  } else {
    if (this->rfile) {
      fclose (this->rfile);
      this->rfile = NULL;
    }
  }
  if (this->rfile) {
    this->read = rip_read_file_read_2;
  } else {
    if (fseeko (this->file, offs2, SEEK_SET)) {
      int e = errno;
      fseeko (this->file, this->savepos, SEEK_SET);
      xine_log (this->stream->xine, XINE_LOG_MSG, _("input_rip: seeking failed: %s\n"), strerror (e));
      return 0;
    }
    if (mode == 1) {
      fseeko (this->file, this->savepos, SEEK_SET);
      this->read = rip_read_file_read_1b;
    } else {
      this->read = rip_read_file_read_1a;
    }
  }
  this->curpos = offs1;
  this->behind = mode;
  xprintf (this->stream->xine, XINE_VERBOSITY_DEBUG,
    "input_rip: reading from %s%s after backseek.\n",
    this->rfile ? "clone of " : "",
    this->fname ? this->fname : "save file");
  return 1;
}

static void rip_read_file_set_2 (rip_input_plugin_t *this) {
  this->behind = 2;
  if (this->rfile) {
    fclose (this->rfile);
    this->rfile = NULL;
  }
  fseeko (this->file, this->curpos, SEEK_SET);
  this->read = rip_read_file_read_1a;
  xprintf (this->stream->xine, XINE_VERBOSITY_DEBUG,
    "input_rip: end of main input, still reading from %s.\n",
    this->fname ? this->fname : "save file");
}

/*
 * read data from input plugin and write it into file
 */
static off_t rip_plugin_read(input_plugin_t *this_gen, void *buf_gen, off_t len) {
  rip_input_plugin_t *this = (rip_input_plugin_t *)this_gen;
  char *buf = (char *)buf_gen;
  off_t d;
  size_t left;

  lprintf("reading %"PRId64" bytes (curpos = %"PRId64", savepos = %"PRId64")\n", len, this->curpos, this->savepos);

  /* bail out on bogus args */
  if (!buf || (len < 0))
    return -1;
  if (len == 0)
    return 0;
  left = len;

  d = this->savepos - this->curpos;
  if (d > 0) {
    if (this->curpos < (off_t)this->preview_size) {
      /* get from preview */
      size_t s2 = this->preview_size - this->curpos;
      if (left < s2)
        s2 = left;
      memcpy (buf, this->preview + this->curpos, s2);
      buf += s2;
      this->curpos += s2;
      left -= s2;
      if (left == 0)
        return s2;
    }
    d = this->savepos - this->curpos;
  }

  /* gcc jump target optimize here ;-) */
  if (d > 0) {
    /* NOTE: size_t (unsigned) may be same size or smaller than off_t (signed). */
    size_t s2 = left;
    d -= (off_t)left;
    if (this->behind) {
      if (d > 0) {
        /* Hair raising naive HACK:
         * read and append left bytes as usual, then return left older bytes from the file.
         * this shall help with inputs that tend to lose track when seeking.
         * OK at least we dont try this in real live mode (behind == 2). bitrate fluctuations
         * and repeated reads like .mp4 fragment scans disharmonize with strict live timing. */
        if (this->behind == 1) {
          ssize_t r = this->main_input_plugin->read (this->main_input_plugin, buf, s2);
          if (r > 0) {
            size_t w = fwrite (buf, 1, r, this->file);
            this->savepos += w;
          } else {
            /* dont stop yet, we still got the rest of the file,
             * and maybe more seeks. */
            rip_read_file_set_2 (this);
            if (this->savepos != this->endpos) {
              this->endpos = this->savepos;
              _x_message (this->stream, XINE_MSG_RECORDING_DONE,
                this->main_input_plugin->get_mrl (this->main_input_plugin), this->fname, NULL);
            }
          }
        }
      } else {
        /* catch up now */
        s2 += d;
      }
      s2 = this->read (this, buf, s2);
      buf += s2;
    } else {
      ssize_t r;
      if (d < 0)
        s2 += d;
      /* read from main input */
      if (this->main_input_plugin->seek (this->main_input_plugin, this->curpos, SEEK_SET) != this->curpos)
        return -1;
      r = this->main_input_plugin->read (this->main_input_plugin, buf, s2);
      if (r != (ssize_t)s2) {
        if (r > 0)
          this->curpos += r;
        xine_log (this->stream->xine, XINE_LOG_MSG,
          _("input_rip: reading by input plugin failed\n"));
        return r;
      }
      buf += s2;
      this->curpos += s2;
    }
    left -= s2;
    if (this->curpos == this->savepos) {
      /* take care of non_seekable _fragments_. */
      if (this->main_input_plugin->get_current_pos (this->main_input_plugin) != this->savepos) {
        off_t have = this->main_input_plugin->seek (this->main_input_plugin, this->savepos, SEEK_SET);
        if (have >= 0) {
          have = this->savepos - have;
          if (have > 0) {
            char temp[4096];
            this->savepos -= have;
            if (fseeko (this->file, this->savepos, SEEK_SET))
              return -1;
            xprintf (this->stream->xine, XINE_VERBOSITY_DEBUG,
              "input_rip: re-reading %" PRId64 " bytes.\n", (int64_t)have);
            while (have > 0) {
              ssize_t r = have > (off_t)sizeof (temp) ? (off_t)sizeof (temp) : have;
              r = this->main_input_plugin->read (this->main_input_plugin, temp, r);
              if (r <= 0)
                break;
              have -= r;
              r = fwrite (temp, 1, r, this->file);
              this->savepos += r;
            }
          }
        }
      }
      /* some stdio implementations need an fseek () between read and write. */
      if (fseeko (this->file, this->savepos, SEEK_SET))
        return -1;
    }
    if (left == 0)
      return buf - (char *)buf_gen;
  }

  {
    /* read from main input, and save to file */
    size_t w = 0;
    int ew = 0;
    ssize_t r = this->main_input_plugin->read (this->main_input_plugin, buf, left);
    if (r > 0) {
      this->curpos += r;
      w = fwrite (buf, 1, r, this->file);
      buf += r;
      ew = errno;
      if (w > 0)
        this->savepos += w;
    }
    if (r != (ssize_t)left) {
      xine_log (this->stream->xine, XINE_LOG_MSG,
        _("input_rip: reading by input plugin failed\n"));
      return r;
    }
    if ((ssize_t)w != r) {
      xine_log (this->stream->xine, XINE_LOG_MSG,
        _("input_rip: error writing to file %" PRIdMAX " bytes: %s\n"),
        (intmax_t)r, strerror (ew));
      return -1;
    }
  }

  return buf - (char *)buf_gen;
}

/*
 * open should never be called
 */
static int rip_plugin_open(input_plugin_t *this_gen) {
  rip_input_plugin_t *this = (rip_input_plugin_t *)this_gen;

  xine_log(this->stream->xine, XINE_LOG_MSG,
           _("input_rip: open() function should never be called\n"));
  return 0;
}

/*
 * set preview and/or seek capability when it's implemented by RIP
 */
static uint32_t rip_plugin_get_capabilities(input_plugin_t *this_gen) {
  rip_input_plugin_t *this = (rip_input_plugin_t *)this_gen;
  uint32_t caps;

  caps = this->main_input_plugin->get_capabilities(this->main_input_plugin);

  if (this->regular)
    caps |= INPUT_CAP_SEEKABLE;

  if (this->preview) caps |= INPUT_CAP_PREVIEW | INPUT_CAP_SIZED_PREVIEW;
  return caps;
}

/*
 * read a block of data from input plugin and write it into file
 *
 * This rip plugin returns block unchanged from main input plugin. But special
 * cases are reading over preview or reading already saved data - it returns
 * own allocated block.
 */
static buf_element_t *rip_plugin_read_block(input_plugin_t *this_gen, fifo_buffer_t *fifo, off_t len) {
  rip_input_plugin_t *this = (rip_input_plugin_t *)this_gen;
  size_t left;

  lprintf("reading %"PRId64" bytes (curpos = %"PRId64", savepos = %"PRId64") (block)\n", len, this->curpos, this->savepos);

  /* bail out on bogus args */
  if (!fifo || (len <= 0))
    return NULL;
  left = len;

  if (this->curpos < this->savepos) {
    buf_element_t *buf = NULL;
    char *q = NULL;
    off_t d;
    size_t s2;
    if ((this->curpos < (off_t)this->preview_size) || this->regular) {
      buf = fifo->buffer_pool_alloc (fifo);
      buf->content = buf->mem;
      buf->type = BUF_DEMUX_BLOCK;
      buf->size = 0;
      if (buf->max_size < (int)left)
        left = buf->max_size;
      q = buf->content;
    }
    if (this->curpos < (off_t)this->preview_size) {
      /* get from preview */
      s2 = this->preview_size - this->curpos;
      if (left < s2)
        s2 = left;
      memcpy (q, this->preview + this->curpos, s2);
      q += s2;
      buf->size = s2;
      this->curpos += s2;
      left -= s2;
      if ((left == 0) || !this->regular)
        return buf;
    }
    d = this->savepos - this->curpos;
    s2 = d <= (off_t)(~(size_t)0) ? (size_t)d : ~(size_t)0;
    if (left < s2)
      s2 = left;
    if (this->behind) {
      /* get from saved file */
      size_t r = this->read (this, q, s2);
      buf->size += r;
      return buf;
    }
    /* read from main input */
    buf = this->main_input_plugin->read_block (this->main_input_plugin, fifo, s2);
    if (buf && (buf->size > 0)) {
      this->curpos += buf->size;
      if (buf->size > (int)s2) {
        /* paranoia?? */
        left = buf->size - s2;
        s2 = fwrite (buf->content + s2, 1, left, this->file);
        this->savepos += s2;
        if (s2 != left) {
          int ew = errno;
          xine_log (this->stream->xine, XINE_LOG_MSG,
            _("input_rip: error writing to file %" PRIdMAX " bytes: %s\n"),
            (intmax_t)left, strerror (ew));
        }
      }
    }
    return buf;
  }

  {
    /* read from main input, and save to file */
    buf_element_t *buf = this->main_input_plugin->read_block (this->main_input_plugin, fifo, left);
    if (buf && (buf->size > 0)) {
      size_t r;
      this->curpos += buf->size;
      r = fwrite (buf->content, 1, buf->size, this->file);
      this->savepos += r;
      if ((int)r != buf->size) {
        int ew = errno;
        xine_log (this->stream->xine, XINE_LOG_MSG,
          _("input_rip: error writing to file %" PRIdMAX " bytes: %s\n"),
          (intmax_t)buf->size, strerror (ew));
      }
    }
    return buf;
  }

}

static off_t rip_seek_original(rip_input_plugin_t *this, off_t reqpos) {
  off_t pos;

  lprintf(" => seeking original input plugin to %"PRId64"\n", reqpos);

  pos = this->main_input_plugin->seek(this->main_input_plugin, reqpos, SEEK_SET);
  if (pos == -1) {
    xine_log(this->stream->xine, XINE_LOG_MSG, _("input_rip: seeking failed\n"));
    return -1;
  }
#ifdef LOG
  if (pos != reqpos) {
    lprintf(CLR_FAIL " => reqested position %"PRId64" differs from result position %"PRId64"" CLR_RST "\n", reqpos, pos);
  }
#endif

  this->curpos = pos;

  return pos;
}

/*
 * seek in RIP
 *
 * If we are seeking back and we can read from saved file,
 * position of original input plugin isn't changed.
 */
static off_t rip_plugin_seek(input_plugin_t *this_gen, off_t offset, int origin) {
  char buffer[SCRATCH_SIZE];
  rip_input_plugin_t *this = (rip_input_plugin_t *)this_gen;
  uint32_t blocksize;
  off_t newpos, pos;
  struct timeval time1, time2;
  double interval = 0;

  lprintf("seek, offset %"PRId64", origin %d (curpos %"PRId64", savepos %"PRId64")\n", offset, origin, this->curpos, this->savepos);

  switch (origin) {
    case SEEK_SET: newpos = offset; break;
    case SEEK_CUR: newpos = this->curpos + offset; break;
    default: newpos = this->curpos;
  }

  /* align the new position down to block sizes */
  if( this_gen->get_capabilities(this_gen) & INPUT_CAP_BLOCK ) {
    blocksize = this_gen->get_blocksize(this_gen);
    newpos = (newpos / blocksize) * blocksize;
  } else
    blocksize = 0;

  if (newpos < this->savepos) {
    lprintf(" => virtual seeking from %"PRId64" to %"PRId64"\n", this->curpos, newpos);

    if (this->regular) {
      if (!rip_read_file_start (this, newpos))
        return -1;
    } else {
      /* don't seek into preview area */
      off_t reqpos = newpos < (off_t)this->preview_size ? (off_t)this->preview_size : newpos;
      if ((pos = rip_seek_original(this, reqpos)) == -1) return -1;
      if (pos == reqpos) this->curpos = newpos;
    }

    return this->curpos;
  }

  if (this->curpos < this->savepos) {
    lprintf(" => seeking to end: %"PRId64"\n", this->savepos);
    if (this->regular) {
      lprintf(" => seeking file to end: %"PRId64"\n", this->savepos);
      if (fseeko(this->file, this->savepos, SEEK_SET) != 0) {
        xine_log(this->stream->xine, XINE_LOG_MSG, _("input_rip: seeking failed: %s\n"), strerror(errno));
        return -1;
      }
      this->curpos = this->savepos;
    } else {
      if ((pos = rip_seek_original(this, this->savepos)) == -1) return -1;
      if (pos > this->savepos)
        xine_log(this->stream->xine, XINE_LOG_MSG,
                 _("input_rip: %" PRIdMAX " bytes dropped\n"),
                 (intmax_t)(pos - this->savepos));
    }
  }

  /* read and catch remaining data after this->savepos */
  xine_monotonic_clock(&time1, NULL);
  while (this->curpos < newpos && interval < SEEK_TIMEOUT) {
    if( blocksize ) {
      buf_element_t *buf;

      buf = rip_plugin_read_block(this_gen, this->stream->video_fifo, blocksize);
      if (buf)
        buf->free_buffer(buf);
      else
        break;
    } else {
      size_t toread = newpos - this->curpos;
      if( toread > sizeof(buffer) )
        toread = sizeof(buffer);

      if( rip_plugin_read(this_gen, buffer, toread) <= 0 ) {
        xine_log(this->stream->xine, XINE_LOG_MSG, _("input_rip: seeking failed\n"));
        break;
      }
    }
    xine_monotonic_clock(&time2, NULL);
    interval = (double)(time2.tv_sec - time1.tv_sec)
               + (double)(time2.tv_usec - time1.tv_usec) / 1000000;
  }

  lprintf(" => new position %"PRId64"\n", this->curpos);

  return this->curpos;
}

static off_t rip_plugin_seek_time(input_plugin_t *this_gen, int time_offset, int origin) {
  rip_input_plugin_t *this = (rip_input_plugin_t *)this_gen;
  off_t r;

  lprintf("seek_time, time_offset: %d, origin: %d\n", time_offset, origin);

  /* HACK: if we have a fragment index, ask for byte offset directly,
   * and keep main input running at the head. */
  do {
    int idx;
    int64_t timebase, timepos, offs;
    if (!this->regular)
      break;
    if (!this->fraglist)
      break;
    timebase = 0;
    xine_mfrag_get_index_frag (this->fraglist, 0, &timebase, NULL);
    if (timebase <= 0)
      break;
    switch (origin) {
      case SEEK_CUR:
        idx = xine_mfrag_find_pos (this->fraglist, this->curpos);
        goto _rip_time_seek_list;
      case SEEK_END:
        idx = xine_mfrag_get_frag_count (this->fraglist) + 1;
      _rip_time_seek_list:
        timepos = 0;
        xine_mfrag_get_index_start (this->fraglist, idx, &timepos, NULL);
        timepos = timepos * 1000 / timebase;
        time_offset += (int)timepos;
        /* fall through */
      case SEEK_SET:
        timepos = (int64_t)time_offset * timebase / 1000;
        idx = xine_mfrag_find_time (this->fraglist, timepos);
        offs = 0;
        xine_mfrag_get_index_start (this->fraglist, idx, &timepos, &offs);
        if (offs < this->savepos)
          rip_read_file_start (this, offs);
        return this->curpos;
      default: ;
    }
  } while (0);

  if (!this->main_input_plugin->seek_time)
    return this->curpos;

  r = this->main_input_plugin->seek_time (this->main_input_plugin, time_offset, origin);
  if ((r >= 0) && (r != this->curpos)) {
    this->curpos = r;
    if (this->regular) {
      off_t s = r;
      if (s < (off_t)this->preview_size) {
        s = this->preview_size;
      } else if (s > this->savepos) {
        s = this->savepos;
      }
      fseeko (this->file, s, SEEK_SET);
    }
  }
  return r;
}

/*
 * return current position,
 * check values for debug build
 */
static off_t rip_plugin_get_current_pos(input_plugin_t *this_gen) {
  rip_input_plugin_t *this = (rip_input_plugin_t *)this_gen;
#ifdef DEBUG
  off_t pos;

  pos = this->main_input_plugin->get_current_pos(this->main_input_plugin);
  if (pos != this->curpos) {
    lprintf(CLR_FAIL "position: computed = %"PRId64", input plugin = %"PRId64"" CLR_RST "\n", this->curpos, pos);
  }
#endif

  return this->curpos;
}

static int rip_plugin_get_current_time(input_plugin_t *this_gen) {
  rip_input_plugin_t *this = (rip_input_plugin_t *)this_gen;

  do {
    int idx, num;
    int64_t timebase, timepos1, timepos2, offs1, offs2;
    if (!this->fraglist)
      break;
    timebase = 0;
    xine_mfrag_get_index_frag (this->fraglist, 0, &timebase, NULL);
    if (timebase <= 0)
      break;
    num = xine_mfrag_get_frag_count (this->fraglist);
    idx = xine_mfrag_find_pos (this->fraglist, this->curpos);
    timepos1 = 0;
    offs1 = 0;
    xine_mfrag_get_index_start (this->fraglist, idx, &timepos1, &offs1);
    if (idx <= num) {
      timepos2 = 0;
      offs2 = 0;
      xine_mfrag_get_index_start (this->fraglist, idx + 1, &timepos2, &offs2);
      timepos1 += (this->curpos - offs1) * (timepos2 - timepos1) / (offs2 - offs1);
    }
    timepos1 = timepos1 * 1000 / timebase;
    return (int)timepos1;
  } while (0);

  if (!this->main_input_plugin->get_current_time)
    return -1;

  return this->main_input_plugin->get_current_time(this->main_input_plugin);
}

static off_t rip_plugin_get_length (input_plugin_t *this_gen) {
  rip_input_plugin_t *this = (rip_input_plugin_t *)this_gen;
  off_t length;

  length = this->main_input_plugin->get_length(this->main_input_plugin);
  if(length <= 0)
    length = this->savepos;

  return length;
}

static uint32_t rip_plugin_get_blocksize(input_plugin_t *this_gen) {
  rip_input_plugin_t *this = (rip_input_plugin_t *)this_gen;

  return this->main_input_plugin->get_blocksize(this->main_input_plugin);
}

static const char* rip_plugin_get_mrl (input_plugin_t *this_gen) {
  rip_input_plugin_t *this = (rip_input_plugin_t *)this_gen;

  return this->main_input_plugin->get_mrl(this->main_input_plugin);
}

static int rip_plugin_get_optional_data (input_plugin_t *this_gen,
					  void *data, int data_type) {
  rip_input_plugin_t *this = (rip_input_plugin_t *)this_gen;
  int r;

  lprintf("get optional data\n");

  r = this->main_input_plugin->get_optional_data (this->main_input_plugin, data, data_type);
  if (r != INPUT_OPTIONAL_UNSUPPORTED)
    return r;

  switch (data_type) {
    case INPUT_OPTIONAL_DATA_PREVIEW:
      if (!this->preview || !data)
        return INPUT_OPTIONAL_UNSUPPORTED;
      memcpy (data, this->preview, this->preview_size);
      return this->preview_size;

    case INPUT_OPTIONAL_DATA_SIZED_PREVIEW:
      if (!this->preview || !data)
        return INPUT_OPTIONAL_UNSUPPORTED;
      memcpy (&r, data, sizeof (r));
      if (r <= 0)
        return INPUT_OPTIONAL_UNSUPPORTED;
      if (r > (int)this->preview_size)
        r = this->preview_size;
      memcpy (data, this->preview, r);
      return r;

    default: ;
  }
  return INPUT_OPTIONAL_UNSUPPORTED;
}

/*
 * dispose main input plugin and self
 */
static void rip_plugin_dispose(input_plugin_t *this_gen) {
  rip_input_plugin_t *this = (rip_input_plugin_t *)this_gen;

  lprintf("rip_plugin_dispose\n");

  _x_free_input_plugin(this->stream, this->main_input_plugin);
  if (this->rfile) {
    fclose (this->rfile);
    this->rfile = NULL;
  }
  if (this->file) {
    fclose (this->file);
    this->file = NULL;
  }
  _x_freep (&this->fname);
  _x_freep(&this->preview);
  free(this);
}


/*
 * create self instance,
 * target file for writing stream is specified in 'data'
 */
input_plugin_t *_x_rip_plugin_get_instance (xine_stream_t *stream, const char *filename) {
  rip_input_plugin_t *this;
  input_plugin_t *main_plugin = stream->input_plugin;
  FILE *file;
  size_t nlen, slen1, slen2;
  char suff1[16], suff2[32], target[4 + MAX_TARGET_LEN + 16 + 32];
  int ptsoffs, regular, i;

  lprintf("catch file = %s, path = %s\n", filename, stream->xine->save_path);

  /* check given input plugin */
  if (!stream->input_plugin) {
    xine_log(stream->xine, XINE_LOG_MSG, _("input_rip: input plugin not defined!\n"));
    return NULL;
  }

  if (!stream->xine->save_path[0]) {
    xine_log(stream->xine, XINE_LOG_MSG,
	     _("input_rip: target directory wasn't specified, please fill out the option 'media.capture.save_dir'\n"));
    _x_message(stream, XINE_MSG_SECURITY,
	       _("The stream save feature is disabled until you set media.capture.save_dir in the configuration."), NULL);
    return NULL;
  }

#ifndef SAVING_ALWAYS_PERMIT
  if ( main_plugin->get_capabilities(main_plugin) & INPUT_CAP_RIP_FORBIDDEN ) {
    xine_log(stream->xine, XINE_LOG_MSG,
	     _("input_rip: ripping/caching of this source is not permitted!\n"));
    _x_message(stream, XINE_MSG_SECURITY,
	       _("xine is not allowed to save from this source. (possibly copyrighted material?)"), NULL);
    return NULL;
  }
#endif

  if (!filename || !filename[0]) {
    xine_log(stream->xine, XINE_LOG_MSG, _("input_rip: file name not given!\n"));
    return NULL;
  }

  {
    char *p;

    nlen = strlen (stream->xine->save_path);
    if (nlen > MAX_TARGET_LEN)
      return NULL;
    memcpy (target + 4, stream->xine->save_path, nlen + 1);
    target[3] = 0;
    for (p = target + 4 + nlen; p[-1] == '/'; p--) ;
    if (p == target + 4) {
      if (*p == '/')
        p++;
    } else {
      *p++ = '/';
    }
    nlen = p - target - 4;
  }

  {
    const char *fn1, *fn2, *fn3, *fn4;

    fn1 = fn2 = fn3 = fn4 = filename;
    while (1) {
      while (*fn4 && (*fn4 != '/'))
        fn4++;
      if (fn4 > fn3) {
        fn1 = fn3;
        fn2 = fn4;
      }
      if (!*fn4)
        break;
      fn4++;
      fn3 = fn4;
    }
    slen1 = fn2 - fn1;
    if (!slen1)
      return NULL;
    if (nlen + slen1 > MAX_TARGET_LEN)
      return NULL;
    memcpy (target + 4 + nlen, fn1, slen1);
    nlen += slen1;
  }

  slen1 = 0;
  suff1[0] = 0;
  ptsoffs = main_plugin->get_optional_data (main_plugin, NULL, INPUT_OPTIONAL_DATA_PTSOFFS);
  if (ptsoffs) {
    slen2 = sprintf (suff2, ".ptsoffs=%d", ptsoffs);
  } else {
    slen2 = 0;
    suff2[0] = 0;
  }

  regular = 1;
  i = 1;
  while (1) {
    struct stat pstat;
    if (slen1)
      xine_small_memcpy (target + 4 + nlen, suff1, slen1);
    if (slen2)
      xine_small_memcpy (target + 4 + nlen + slen1, suff2, slen2);
    target[4 + nlen + slen1 + slen2] = 0;
    /* find out kind of target */
    if (stat (target + 4, &pstat) < 0)
      break;
#ifndef _MSC_VER
    regular = (S_ISFIFO (pstat.st_mode)) ? 0 : 1;
    if (!regular) {
      /* we want write into fifos */
      break;
    }
#else
    /* no fifos under MSVC */
#endif
    slen1 = sprintf (suff1, ".%d", i);
    i++;
  };

  lprintf ("target file: %s\n", target + 4);
  file = fopen (target + 4, regular ? "wb+" : "wb");
  if (!file) {
    int e = errno;
    xine_log (stream->xine, XINE_LOG_MSG,
        _("input_rip: error opening file %s: %s\n"), target + 4, strerror (e));
    return NULL;
  }

  this = calloc (1, sizeof (*this));
  if (!this) {
    fclose (file);
    return NULL;
  }

  this->main_input_plugin = main_plugin;
  this->stream            = stream;
  this->file              = file;
  this->fname             = strdup (target + 4);
  this->regular           = regular;
  this->read              = rip_read_file_read_1a;
#ifndef HAVE_ZERO_SAFE_MEM
  this->rfile        = NULL;
  this->fraglist     = NULL;
  this->preview      = NULL;
  this->behind       = 0;
  this->curpos       = 0;
  this->savepos      = 0;
  this->endpos       = 0;
  this->preview_size = 0;
#endif

  /* fill preview memory */
  {
    uint32_t caps = this->main_input_plugin->get_capabilities (this->main_input_plugin);
    if (!(caps & (INPUT_CAP_SEEKABLE | INPUT_CAP_PREVIEW | INPUT_CAP_SIZED_PREVIEW))) {
      if (caps & INPUT_CAP_BLOCK) {
        buf_element_t *buf;
        uint32_t blocksize;

        blocksize = main_plugin->get_blocksize (main_plugin);
        buf = main_plugin->read_block (main_plugin, stream->video_fifo, blocksize);
        this->preview = malloc (buf->size);
        if (this->preview) {
          this->preview_size = buf->size;
          memcpy (this->preview, buf->content, this->preview_size);
        }
        buf->free_buffer (buf);
      } else {
        this->preview = malloc (MAX_PREVIEW_SIZE);
        if (this->preview) {
          ssize_t r = main_plugin->read (main_plugin, this->preview, MAX_PREVIEW_SIZE);
          if (r > 0) {
            this->preview_size = r;
          } else {
            _x_freep (&this->preview);
          }
        }
      }
    }
  }

  if (this->preview_size) {
    if (fwrite (this->preview, 1, this->preview_size, this->file) != this->preview_size) {
      int e = errno;
      xine_log (this->stream->xine, XINE_LOG_MSG,
        _("input_rip: error writing to file %" PRIdMAX " bytes: %s\n"),
        (intmax_t)(this->preview_size), strerror (e));
      fclose (this->file);
      _x_freep (&this->preview);
      free (this);
      return NULL;
    }
    lprintf(" => saved %u bytes (preview)\n", (unsigned int)this->preview_size);
    this->savepos = this->preview_size;
  }

  {
    xine_mfrag_list_t *list = NULL;
    if (this->main_input_plugin->get_optional_data (this->main_input_plugin, &list, INPUT_OPTIONAL_DATA_FRAGLIST)
      == INPUT_OPTIONAL_SUCCESS)
      this->fraglist = list;
  }

  this->input_plugin.open                = rip_plugin_open;
  this->input_plugin.get_capabilities    = rip_plugin_get_capabilities;
  this->input_plugin.read                = rip_plugin_read;
  this->input_plugin.read_block          = rip_plugin_read_block;
  this->input_plugin.seek                = rip_plugin_seek;
  this->input_plugin.seek_time           = rip_plugin_seek_time;
  this->input_plugin.get_current_pos     = rip_plugin_get_current_pos;
  this->input_plugin.get_current_time    = rip_plugin_get_current_time;
  this->input_plugin.get_length          = rip_plugin_get_length;
  this->input_plugin.get_blocksize       = rip_plugin_get_blocksize;
  this->input_plugin.get_mrl             = rip_plugin_get_mrl;
  this->input_plugin.get_optional_data   = rip_plugin_get_optional_data;
  this->input_plugin.dispose             = rip_plugin_dispose;
  this->input_plugin.input_class         = main_plugin->input_class;

  return &this->input_plugin;
}

