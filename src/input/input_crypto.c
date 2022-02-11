/*
 * Copyright (C) 2000-2022 the xine project
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
 * Transparent decryption of input data
 *
 * AES
 * Key bits: 128, 192 or 256.
 * CBC (iv given), ECB (no iv)
 *
 * Examples:
 *   xine crypto:key=xxx:file://path/to/file.ts
 *   xine crypto:key=xxx:http://www.example.org/file.ts
 *   xine crypto:key=xxx:iv=yyy:file:/path/file.ext
 *
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <gcrypt.h>

#define LOG_MODULE "input_crypto"
#define LOG_VERBOSE
/*
#define LOG
*/

#include <xine/xine_internal.h>
#include <xine/xineutils.h>
#include <xine/input_plugin.h>
#include "input_helper.h"

#define CRYPTO_BLOCK_SIZE 4096

#define CHECK(x)
//#define CHECK(x) _x_assert(x)

typedef struct {
  input_plugin_t   input_plugin;

  xine_stream_t   *stream;
  char            *mrl;
  input_plugin_t  *in0;

  gcry_cipher_hd_t gcry_h;

  off_t            curpos;
  off_t            block_start;
  off_t            block_size;
  uint8_t          block[CRYPTO_BLOCK_SIZE];
  int              eof;

  size_t           iv_len;
  uint8_t          iv[16];

  size_t           key_len;
  uint8_t          key[32];

} crypto_input_plugin_t;

static void _fill(crypto_input_plugin_t *this)
{
  unsigned have = 0, had = 0;
  gcry_error_t err;

  if (this->eof)
    return;

  CHECK(this->block_start + this->block_size == this->in0->get_current_pos(this->in0));

  /* keep unread part of current buffer (we need to overread for EOF padding detection) */
  if (this->curpos >= this->block_start && this->curpos < this->block_start + this->block_size) {
    have = had = this->block_start + this->block_size - this->curpos;
    memmove(this->block, this->block + this->block_size - had, had);
  }
  this->block_start += this->block_size - had;

  /* read */
  while (have < sizeof(this->block)) {
    off_t got = this->in0->read(this->in0, this->block + have, sizeof(this->block) - have);
    if (got <= 0) {
      if (got == 0)
        this->eof = 1;
      break;
    }
    have += got;
  }
  this->block_size = have;

  /* decrypt */
  if (have > had) {
    err = gcry_cipher_decrypt(this->gcry_h, this->block + had, this->block_size - had, NULL, 0);
    if (err)
      xprintf(this->stream->xine, XINE_VERBOSITY_LOG, LOG_MODULE ": "
              "Error decrypting data: %d\n", err);
  }

  /* Drop possible PKCS7 padding at the end */
  if (this->eof && this->block_size > 0)
    this->block_size -= this->block[this->block_size - 1];

  lprintf("filled: block_start %" PRId64 ", size %u, in0_pos %" PRId64 "\n",
          (int64_t)this->block_start, (unsigned)this->block_size,
          (int64_t)this->in0->get_current_pos(this->in0));
}

static off_t crypto_plugin_read (input_plugin_t *this_gen, void *buf_gen, off_t len)
{
  crypto_input_plugin_t *this = xine_container_of(this_gen, crypto_input_plugin_t, input_plugin);
  off_t have = 0, block_pos, chunk_size;

  CHECK(this->curpos >= this->block_start);

  while (have < len) {
    /* refill cache ? */
    if (this->curpos >= this->block_start + this->block_size - 16 /* keep one block for padding check */) {
      _fill(this);
      if (this->curpos >= this->block_start + this->block_size)
        break;
    }

    block_pos  = this->curpos - this->block_start;
    chunk_size = MIN(len - have, this->block_size - block_pos);
    if (!this->eof && chunk_size > 16)
      chunk_size -= 16; /* delay last block to detect padding */

    memcpy((uint8_t *)buf_gen + have, this->block + block_pos, chunk_size);
    have += chunk_size;
    this->curpos += chunk_size;
  }

  return have;
}

static uint32_t crypto_plugin_get_blocksize (input_plugin_t *this_gen)
{
  crypto_input_plugin_t *this = xine_container_of(this_gen, crypto_input_plugin_t, input_plugin);
  return this->in0->get_blocksize(this->in0);
}

static off_t crypto_plugin_get_current_pos (input_plugin_t *this_gen)
{
  crypto_input_plugin_t *this = xine_container_of(this_gen, crypto_input_plugin_t, input_plugin);
  return this->curpos;
}

static off_t crypto_plugin_seek (input_plugin_t *this_gen, off_t offset, int origin)
{
  crypto_input_plugin_t *this = xine_container_of(this_gen, crypto_input_plugin_t, input_plugin);
  off_t res, in0_length = this->in0->get_length(this->in0);
  unsigned to_skip;

  res = _x_input_translate_seek(offset, origin, this->curpos, in0_length);
  if (res < 0)
    return res;

  /* check if seeking inside internal buffer */
  if (offset >= this->block_start && offset < this->block_start + this->block_size) {
    this->curpos = offset;
    return offset;
  }

  /* invalidate cache */
  this->block_size = 0;
  this->eof = 0;

  /* align to block boundary */
  to_skip = offset & 0x0f;
  if (this->iv_len) {
    /* need to set or generate iv, read also previous block */
    if (offset < 16) {
      gcry_error_t err;
      err = gcry_cipher_setiv(this->gcry_h, this->iv, this->iv_len);
      if (err)
        xprintf(this->stream->xine, XINE_VERBOSITY_LOG, LOG_MODULE ": "
                "Error setting cipher iv: %d\n", err);
    } else {
      /* resync iv by reading and decrypting prev block */
      to_skip += 16;
    }
  }

  lprintf("seek to %ld, skip=%u\n", (int64_t)(offset - to_skip), to_skip);

  /* seek to block boundary */
  res = this->in0->seek(this->in0, offset - to_skip, SEEK_SET);
  if (res < 0) {
    return res;
  }
  this->block_start = offset - to_skip;
  this->curpos = offset;

  /* re-gen iv: refill cache and drop first (broken) block from cache */
  if (to_skip > 16) {
    _fill(this);
    if (this->block_size >= 16) {
      memmove(this->block, this->block + 16, this->block_size - 16);
      this->block_size -= 16;
      this->block_start += 16;
    }
  }

  return this->curpos;
}

static uint32_t crypto_plugin_get_capabilities (input_plugin_t *this_gen)
{
  crypto_input_plugin_t *this = xine_container_of(this_gen, crypto_input_plugin_t, input_plugin);
  return this->in0->get_capabilities(this->in0) | INPUT_CAP_PREVIEW | INPUT_CAP_SIZED_PREVIEW;
}

static off_t crypto_plugin_get_length (input_plugin_t *this_gen)
{
  crypto_input_plugin_t *this = xine_container_of(this_gen, crypto_input_plugin_t, input_plugin);
  return this->in0->get_length(this->in0);
}

static const char* crypto_plugin_get_mrl (input_plugin_t *this_gen) {
  crypto_input_plugin_t *this = xine_container_of(this_gen, crypto_input_plugin_t, input_plugin);
  return this->mrl;
}

static int crypto_plugin_get_optional_data (input_plugin_t *this_gen, void *data, int data_type) {
  crypto_input_plugin_t *this = xine_container_of(this_gen, crypto_input_plugin_t, input_plugin);
  int result = INPUT_OPTIONAL_UNSUPPORTED;

  switch (data_type) {
    case INPUT_OPTIONAL_DATA_PREVIEW:
      crypto_plugin_seek(this_gen, 0, SEEK_SET);
      result = this->block_size < MAX_PREVIEW_SIZE ? this->block_size : MAX_PREVIEW_SIZE;
      memcpy (data, this->block, result);
      break;
    case INPUT_OPTIONAL_DATA_SIZED_PREVIEW:
      crypto_plugin_seek(this_gen, 0, SEEK_SET);
      memcpy (&result, data, sizeof (result));
      if (result <= 0)
        return INPUT_OPTIONAL_UNSUPPORTED;
      if (result > (int)this->block_size)
        result = this->block_size;
      memcpy (data, this->block, result);
      return result;
    case INPUT_OPTIONAL_DATA_NEW_MRL:
    case INPUT_OPTIONAL_DATA_CLONE:
      return INPUT_OPTIONAL_UNSUPPORTED;
    default:
      break;
  }

  return this->in0->get_optional_data(this->in0, data, data_type);
}

static void crypto_plugin_dispose (input_plugin_t *this_gen )
{
  crypto_input_plugin_t *this = xine_container_of(this_gen, crypto_input_plugin_t, input_plugin);

  gcry_cipher_close(this->gcry_h);

  _x_free_input_plugin (this->stream, this->in0);
  _x_freep (&this->mrl);
  free (this_gen);
}

static int crypto_plugin_open (input_plugin_t *this_gen)
{
  crypto_input_plugin_t *this = xine_container_of(this_gen, crypto_input_plugin_t, input_plugin);
  gcry_error_t err;

  if (!this->in0->open (this->in0))
    return 0;

  if (this->iv_len)
    err = gcry_cipher_open(&this->gcry_h, GCRY_CIPHER_AES, GCRY_CIPHER_MODE_CBC, 0);
  else
    err = gcry_cipher_open(&this->gcry_h, GCRY_CIPHER_AES, GCRY_CIPHER_MODE_ECB, 0);
  if (err)
    goto error;

  err = gcry_cipher_setkey(this->gcry_h, this->key, this->key_len);
  if (err)
    goto error;

  if (this->iv_len) {
    err = gcry_cipher_setiv(this->gcry_h, this->iv, this->iv_len);
    if (err)
      goto error;
  }

  this->curpos = 0;
  return 1;

 error:
  xprintf(this->stream->xine, XINE_VERBOSITY_LOG, LOG_MODULE ": "
          "Error setting cipher: %d\n", err);
  return 0;
}

static unsigned _hexval(const char *c)
{
  return ((unsigned)(*c -'0') <= 9) ? (unsigned)(*c - '0') :
         ((unsigned)((*c | 0x20) - 'a') < 6) ? (unsigned)((*c | 0x20) - 'a') + 10 :
         (unsigned)-1;
}

static int _get_hex(const char *src, uint8_t *dst)
{
  unsigned v = (_hexval(src) << 4) | _hexval(src + 1);
  *dst = v;
  return - (v >> 8);
}

static size_t _get_key(const char *src, uint8_t *dst, size_t dst_size)
{
  size_t len;
  for (len = 0; src[2*len] && len < dst_size; len++)
    if (_get_hex(src + 2*len, dst + len) < 0)
      break;
  return src[2*len] == ':' ? len : 0;
}

static input_plugin_t *crypto_class_get_instance (input_class_t *cls_gen, xine_stream_t *stream, const char *mrl)
{
  crypto_input_plugin_t *this;
  input_plugin_t        *in0;
  const char            *sub_mrl, *key = NULL, *iv = NULL, *p;
  size_t                 key_len, iv_len;
  uint8_t                aes_key[32], aes_iv[16];

  if (strncasecmp (mrl, "crypto:", 7))
    return NULL;

  /* get sub-mrl */
  sub_mrl = strstr(mrl, ":/");
  if (!sub_mrl)
    return 0;
  while (sub_mrl > mrl && sub_mrl[-1] != ':') /* rewind protocol part */
    sub_mrl--;

  /* parse key */
  for (p = strchr(mrl, ':') + 1; p < sub_mrl; p = strchr(p, ':') + 1) {
    if (!strncmp(p, "key=", 4)) {
      key = p + 4;
    } else if (!strncmp(p, "iv=", 3)) {
      iv = p + 3;
    } else {
      xprintf(stream->xine, XINE_VERBOSITY_LOG, LOG_MODULE ": Unknown option at %s\n", p);
      return NULL;
    }
  }
  if (!key) {
    xprintf(stream->xine, XINE_VERBOSITY_LOG, LOG_MODULE ": No key privided in mrl\n");
    return NULL;
  }

  key_len = _get_key(key, aes_key, sizeof(aes_key));
  if (key_len != 16 && key_len != 24 && key_len != 32) {
    xprintf(stream->xine, XINE_VERBOSITY_LOG, LOG_MODULE ": "
            "unsupported key (length %zu)\n", key_len);
    return NULL;
  }

  iv_len = iv ? _get_key(iv, aes_iv, sizeof(aes_iv)) : 0;
  if (iv_len != 0 && iv_len != 16) {
    xprintf(stream->xine, XINE_VERBOSITY_LOG, LOG_MODULE ": "
            "unsupported IV (length %zu)\n", iv_len);
    return NULL;
  }

  /* find real input */
  in0 = _x_find_input_plugin (stream, sub_mrl);
  if (!in0)
    return NULL;

  this = calloc(1, sizeof(crypto_input_plugin_t));
  if (!this) {
    _x_free_input_plugin (stream, in0);
    return NULL;
  }

  this->mrl           = strdup(sub_mrl);
  this->stream        = stream;
  this->curpos        = 0;
  this->in0           = in0;
  this->key_len       = key_len;
  this->iv_len        = iv_len;

  memcpy(this->key, aes_key, key_len);
  if (iv_len) {
    memcpy(this->iv, aes_iv, iv_len);
  }

  if (!this->mrl) {
    _x_free_input_plugin (stream, in0);
    free(this);
    return NULL;
  }

  this->input_plugin.open              = crypto_plugin_open;
  this->input_plugin.get_capabilities  = crypto_plugin_get_capabilities;
  this->input_plugin.read              = crypto_plugin_read;
  this->input_plugin.read_block        = _x_input_default_read_block;
  this->input_plugin.seek              = crypto_plugin_seek;
  this->input_plugin.get_current_pos   = crypto_plugin_get_current_pos;
  this->input_plugin.get_length        = crypto_plugin_get_length;
  this->input_plugin.get_blocksize     = crypto_plugin_get_blocksize;
  this->input_plugin.get_mrl           = crypto_plugin_get_mrl;
  this->input_plugin.get_optional_data = crypto_plugin_get_optional_data;
  this->input_plugin.dispose           = crypto_plugin_dispose;
  this->input_plugin.input_class       = cls_gen;

  return &this->input_plugin;
}


/*
 *  plugin class
 */

static void *input_crypto_init_class (xine_t *xine, const void *data)
{
  static const input_class_t input_crypto_class = {
    .get_instance      = crypto_class_get_instance,
    .description       = N_("crypto input plugin wrapper"),
    .identifier        = "crypto",
    .get_dir           = NULL,
    .get_autoplay_list = NULL,
    .dispose           = NULL,
    .eject_media       = NULL,
  };

  (void)xine;
  (void)data;
  return (void *)&input_crypto_class;
}

/*
 * exported plugin catalog entry
 */

const plugin_info_t xine_plugin_info[] EXPORTED = {
  /* type, API, "name", version, special_info, init_function */
  { PLUGIN_INPUT, 18, "crypto", XINE_VERSION_CODE, NULL, input_crypto_init_class },
  { PLUGIN_NONE, 0, NULL, 0, NULL, NULL }
};
