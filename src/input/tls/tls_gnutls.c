/*
 * Copyright (C) 2000-2019 the xine project
 * Copyright (C) 2018 Petri Hintukainen <phintuka@users.sourceforge.net>
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
 */

/*
 * xine TLS provider plugin (using gnutls)
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdlib.h>
#include <errno.h>

#include <gnutls/gnutls.h>
#include <gnutls/x509.h>
#if defined (GNUTLS_VERSION_NUMBER) && (GNUTLS_VERSION_NUMBER >= 0x030000)
#  define XINE_GNUTLS_3
#  define XINE_GNUTLS_INIT_FLAGS GNUTLS_CLIENT | GNUTLS_NONBLOCK
#else
#  define XINE_GNUTLS_INIT_FLAGS GNUTLS_CLIENT
#endif

#ifndef XINE_GNUTLS_3
#  ifdef HAVE_MALLOC_H
#    include <malloc.h>
#  endif
#  include <string.h>
#  include <sys/stat.h>
#  include <dirent.h>
#endif

#define LOG_MODULE "gnutls"

#include <xine/xine_internal.h>
#include <xine/xineutils.h>
#include <xine/io_helper.h>

#include <xine/xine_plugin.h>

#include "xine_tls_plugin.h"

typedef struct {
  tls_plugin_t tls_plugin;

  xine_stream_t *stream;
  xine_t        *xine;

  int fd;
  int need_shutdown;

  gnutls_session_t session;
  gnutls_certificate_credentials_t cred;

  size_t buf_got, buf_delivered;
  uint8_t buf[32 << 10];

} tls_gnutls_t;

/*
 * gnutls I/O callbacks
 */

static ssize_t gnutls_tcp_pull(gnutls_transport_ptr_t tp,
                               void *buf, size_t len)
{
  /* gnutls always reads small block head (5 bytes), gets payload size,
   * and reads the rest of block before it does anything else. */
  tls_gnutls_t *this = (tls_gnutls_t *)tp;
  size_t l = this->buf_got - this->buf_delivered;
  if (l) {
    /* get from buf */
    if (l > len) {
      xine_fast_memcpy (buf, this->buf + this->buf_delivered, len);
      this->buf_delivered += len;
      return len;
    }
    xine_fast_memcpy (buf, this->buf + this->buf_delivered, l);
    this->buf_got = this->buf_delivered = 0;
    return l;
  }
  /* buf is empty */
  if (len < 17) {
    /* head only, read ahead ;-) */
    ssize_t r = _x_io_tcp_part_read (this->stream, this->fd, this->buf, len, sizeof (this->buf));
    if (r <= 0)
      return r;
    if ((size_t)r > len) {
      xine_small_memcpy (buf, this->buf, len);
      this->buf_got = r;
      this->buf_delivered = len;
      return len;
    }
    xine_small_memcpy (buf, this->buf, r);
    return r;
  }
  /* get directly */
  return _x_io_tcp_read (this->stream, this->fd, buf, len);
}

static ssize_t gnutls_tcp_push(gnutls_transport_ptr_t tp,
                               const void *buf, size_t len)
{
  tls_gnutls_t *this = (tls_gnutls_t *)tp;
  return _x_io_tcp_write(this->stream, this->fd, buf, len);
}

/*
 *
 */
#if 0
static int handle_gnutls_error(tls_gnutls_t *this, int err)
{
  switch (err) {
    case GNUTLS_E_AGAIN:
      errno = EAGAIN;
      return -1;
    default:
      xprintf(this->xine, XINE_VERBOSITY_LOG, LOG_MODULE ": %s\n", gnutls_strerror(err));
      break;
  }

  errno = EIO;
  return -1;
}
#endif
/*
 *
 */

static ssize_t _gnutls_write(tls_plugin_t *this_gen, const void *buf, size_t len)
{
  tls_gnutls_t *this = (tls_gnutls_t *)this_gen;
  const uint8_t *b = (const uint8_t *)buf;
  size_t have = 0;

  if (!this->session)
    return -1;
  while (have < len) {
    ssize_t ret = gnutls_record_send (this->session, b + have, len - have);
    if (ret > 0) {
      have += ret;
    } else if (ret == 0) {
      break;
    } else if (ret == GNUTLS_E_AGAIN) {
      ret = _x_io_select (this->stream, this->fd,
              gnutls_record_get_direction (this->session) ? XIO_WRITE_READY : XIO_READ_READY,
              _x_query_network_timeout (this->xine) * 1000);
      if (ret != XIO_READY)
        return -1;
    } else {
      xprintf (this->xine, XINE_VERBOSITY_LOG, LOG_MODULE ": %s (%d).\n", gnutls_strerror (ret), (int)ret);
      errno = EIO;
      return -1;
    }
  }
  return have;
}

static ssize_t _gnutls_read(tls_plugin_t *this_gen, void *buf, size_t len)
{
  tls_gnutls_t *this = (tls_gnutls_t *)this_gen;
  uint8_t *b = (uint8_t *)buf;
  size_t have = 0;

  if (!this->session)
    return -1;
  while (have < len) {
    ssize_t ret = gnutls_record_recv (this->session, b + have, len - have);
    if (ret > 0) {
      have += ret;
    } else if (ret == 0) {
      break;
    } else if (ret == GNUTLS_E_AGAIN) {
      ret = _x_io_select (this->stream, this->fd,
              gnutls_record_get_direction (this->session) ? XIO_WRITE_READY : XIO_READ_READY,
              _x_query_network_timeout (this->xine) * 1000);
      if (ret != XIO_READY)
        return -1;
    } else {
      xprintf (this->xine, XINE_VERBOSITY_LOG, LOG_MODULE ": %s (%d).\n", gnutls_strerror (ret), (int)ret);
      errno = EIO;
      return -1;
    }
  }
  return have;
}

static ssize_t _gnutls_part_read (tls_plugin_t *this_gen, void *buf, size_t min, size_t max) {
  tls_gnutls_t *this = (tls_gnutls_t *)this_gen;
  uint8_t *b = (uint8_t *)buf;
  size_t have = 0;

  if (!this->session)
    return -1;
  while (have < min) {
    ssize_t ret = gnutls_record_recv (this->session, b + have, max - have);
    if (ret > 0) {
      have += ret;
    } else if (ret == 0) {
      break;
    } else if (ret == GNUTLS_E_AGAIN) {
      ret = _x_io_select (this->stream, this->fd,
              gnutls_record_get_direction (this->session) ? XIO_WRITE_READY : XIO_READ_READY,
              _x_query_network_timeout (this->xine) * 1000);
      if (ret != XIO_READY)
        return -1;
    } else {
      xprintf (this->xine, XINE_VERBOSITY_LOG, LOG_MODULE ": %s (%d).\n", gnutls_strerror (ret), (int)ret);
      errno = EIO;
      return -1;
    }
  }
  return have;
}

#ifndef XINE_GNUTLS_3
static int _gnutls_load_certs (tls_gnutls_t *this, const char *dirname) {
  DIR *dir;
  dir = opendir (dirname);
  if (!dir) {
    int e = errno;
    xprintf (this->xine, XINE_VERBOSITY_LOG, "tls_gnutls: %s: %s (%d).\n", dirname, strerror (e), e);
    return 1;
  }
  {
#if _FILE_OFFSET_BITS == 64
#  define list_t uint64_t
#else
#  define list_t uint32_t
#endif
    unsigned int have_num, certs, files, dupl;
    struct dirent *item;
    list_t *have_list;
    uint8_t tempbuf[2048], *nadd = tempbuf;
    nadd += strlcpy ((char *)tempbuf, dirname, sizeof (tempbuf) - 2);
    if (nadd > tempbuf + sizeof (tempbuf) - 2)
      nadd = tempbuf + sizeof (tempbuf) - 2;
    *nadd++ = '/';
    have_list = malloc (512 * sizeof (list_t));
    if (!have_list) {
      closedir (dir);
      return 2;
    }
    have_list[0] = ~(list_t)0;
    have_num = 0;
    certs = 0;
    files = 0;
    dupl = 0;
    while ((item = readdir (dir)) != NULL) {
      struct stat s;
      int n;
      if (!item->d_name)
        continue;
      strlcpy ((char *)nadd, item->d_name, sizeof (tempbuf) - (nadd - tempbuf));
      if (stat ((const char *)tempbuf, &s))
        continue;
      if (!S_ISREG (s.st_mode))
        continue;
      files++;
      /* Simple avoid duplicates by inode #. What about symbolic links? */
      {
        list_t *here, i = s.st_ino;
        unsigned int a = 0, l, m = ~0, e = have_num + 1;
        while (1) {
          l = m;
          m = (a + e) >> 1;
          if (l == m)
            break;
          if (i < have_list[m])
            e = m;
          else if (i > have_list[m])
            a = m;
          else
            break;
        }
        here = have_list + m;
        if (i == *here) {
          dupl++;
          continue;
        }
        do {
          if ((have_num & 511) == 511) {
            here = realloc (have_list, (have_num + 1 + 512) * sizeof (list_t));
            if (!here)
              break;
            have_list = here;
            here += m;
          }
          memmove (here + 1, here, (have_num - m + 1) * sizeof (list_t));
          *here = i;
          have_num++;
        } while (0);
      }
      n = gnutls_certificate_set_x509_trust_file (this->cred, (const char *)tempbuf, GNUTLS_X509_FMT_PEM);
      if (n < 0)
        xprintf (this->xine, XINE_VERBOSITY_LOG,
          "tls_gnutls: %s: %s (%d).\n", (const char *)tempbuf, gnutls_strerror (n), n);
      else
        certs += n;
    }
#undef list_t
    free (have_list);
    closedir (dir);
    xprintf (this->xine, XINE_VERBOSITY_DEBUG,
      "tls_gnutls: %s: got %u files, %u duplicates.\n", dirname, files, dupl);
    if (!certs)
      xprintf (this->xine, XINE_VERBOSITY_LOG,
        "tls_gnutls: %s: no trust certificates found.\n", dirname);
    else
      xprintf (this->xine, XINE_VERBOSITY_DEBUG,
        "tls_gnutls: loaded %u trust certificates.\n", certs);
  }
  return 0;
}
#endif

static void _gnutls_shutdown(tls_plugin_t *this_gen)
{
  tls_gnutls_t *this = (tls_gnutls_t *)this_gen;

  if (this->need_shutdown) {
    this->need_shutdown = 0;
    while (1) {
      int ret = gnutls_bye (this->session, GNUTLS_SHUT_WR);
      if (ret != GNUTLS_E_AGAIN)
        break;
      ret = _x_io_select (this->stream, this->fd,
        gnutls_record_get_direction (this->session) ? XIO_WRITE_READY : XIO_READ_READY,
        _x_query_network_timeout (this->xine) * 1000);
      if (ret != XIO_READY)
        break;
    }
  }
  if (this->session) {
    gnutls_deinit(this->session);
    this->session = NULL;
  }
  if (this->cred) {
    gnutls_certificate_free_credentials(this->cred);
    this->cred = NULL;
  }
}

static int _gnutls_handshake(tls_plugin_t *this_gen, const char *host, int verify)
{
  tls_gnutls_t *this = (tls_gnutls_t *)this_gen;
  int ret;

  _x_assert(this->session == NULL);

  gnutls_init (&this->session, XINE_GNUTLS_INIT_FLAGS);
  if (host) {
    gnutls_server_name_set(this->session, GNUTLS_NAME_DNS, host, strlen(host));
  }

  gnutls_certificate_allocate_credentials(&this->cred);
#ifdef XINE_GNUTLS_3
  gnutls_certificate_set_x509_system_trust(this->cred);
#else
  _gnutls_load_certs (this, "/etc/ssl/certs");
#endif
  gnutls_certificate_set_verify_flags(this->cred, verify ? GNUTLS_VERIFY_ALLOW_X509_V1_CA_CRT : 0);
  gnutls_credentials_set(this->session, GNUTLS_CRD_CERTIFICATE, this->cred);

  gnutls_transport_set_pull_function(this->session, gnutls_tcp_pull);
  gnutls_transport_set_push_function(this->session, gnutls_tcp_push);
  gnutls_transport_set_ptr(this->session, this);
#ifndef XINE_GNUTLS_3
  gnutls_transport_set_lowat (this->session, 0);
#endif

  gnutls_priority_set_direct(this->session, "NORMAL", NULL);

  while (1) {
    ret = gnutls_handshake (this->session);
    if (ret != GNUTLS_E_AGAIN)
      break;
    ret = _x_io_select (this->stream, this->fd,
            gnutls_record_get_direction (this->session) ? XIO_WRITE_READY : XIO_READ_READY,
            _x_query_network_timeout (this->xine) * 1000);
    if (ret != XIO_READY)
      return -1;
  }
  if (ret) {
    xprintf (this->xine, XINE_VERBOSITY_LOG, LOG_MODULE ": "
      "TLS handshake failed: %s (%d)\n", gnutls_strerror (ret), ret);
    return -1;
  }

  this->need_shutdown = 1;

  if (verify < 0 && this->xine)
    verify = tls_get_verify_tls_cert(this->xine->config);

  if (verify) {
    unsigned int status;
    while (1) {
      ret = gnutls_certificate_verify_peers2 (this->session, &status);
      if (ret != GNUTLS_E_AGAIN)
        break;
      ret = _x_io_select (this->stream, this->fd,
              gnutls_record_get_direction (this->session) ? XIO_WRITE_READY : XIO_READ_READY,
              _x_query_network_timeout (this->xine) * 1000);
      if (ret != XIO_READY)
        return -2;
    }
    if (ret < 0) {
      xprintf(this->xine, XINE_VERBOSITY_LOG, LOG_MODULE ": "
              "Unable to verify peer certificate: %s (%d)\n",
              gnutls_strerror(ret), ret);
      return -2;
    }
    if (status & GNUTLS_CERT_INVALID) {
      xprintf(this->xine, XINE_VERBOSITY_LOG, LOG_MODULE ": "
              "Peer certificate failed verification\n");
      return -2;
    }
    if (gnutls_certificate_type_get(this->session) != GNUTLS_CRT_X509) {
      xprintf(this->xine, XINE_VERBOSITY_LOG, LOG_MODULE ": "
              "Unsupported certificate type\n");
      return -2;
    }
    if (host) {
      unsigned int          cert_list_size;
      gnutls_x509_crt_t     cert;
      const gnutls_datum_t *cert_list;
      gnutls_x509_crt_init(&cert);
      cert_list = gnutls_certificate_get_peers(this->session, &cert_list_size);
      gnutls_x509_crt_import(cert, cert_list, GNUTLS_X509_FMT_DER);
      ret = gnutls_x509_crt_check_hostname(cert, host);
      gnutls_x509_crt_deinit(cert);
      if (!ret) {
        xprintf(this->xine, XINE_VERBOSITY_LOG, LOG_MODULE ": "
                "The certificate does not match hostname %s\n",
                host);
        return -3;
      }
    }
  }

  return 0;
}

static void _gnutls_dispose(xine_module_t *this_gen)
{
  _gnutls_shutdown((tls_plugin_t*)this_gen);

  gnutls_global_deinit();

  free(this_gen);
}

static xine_module_t *gnutls_get_instance(xine_module_class_t *cls_gen, const void *params_gen)
{
  const tls_plugin_params_t *p = params_gen;
  tls_gnutls_t *this;
  int ret;

  (void)cls_gen;

  ret = gnutls_global_init();
  if (ret) {
    xprintf(p->xine, XINE_VERBOSITY_LOG, LOG_MODULE ": "
            "gnutls_global_init() failed: %s (%d)\n",
            gnutls_strerror(ret), ret);
    return NULL;
  }

  this = calloc(1, sizeof(*this));
  if (!this) {
    gnutls_global_deinit();
    return NULL;
  }

  this->tls_plugin.module.dispose = _gnutls_dispose;

  this->tls_plugin.handshake = _gnutls_handshake;
  this->tls_plugin.shutdown  = _gnutls_shutdown;
  this->tls_plugin.read      = _gnutls_read;
  this->tls_plugin.part_read = _gnutls_part_read;
  this->tls_plugin.write     = _gnutls_write;

  this->xine   = p->xine;
  this->fd     = p->fd;
  this->stream = p->stream;

  this->buf_got = 0;
  this->buf_delivered = 0;

  return &this->tls_plugin.module;
}

static void *gnutls_init_class(xine_t *xine, const void *data)
{
  static const xine_module_class_t tls_gnutls_class = {
    .get_instance      = gnutls_get_instance,
    .description       = N_("TLS provider (gnutls)"),
    .identifier        = "gnutls",
    .dispose           = NULL,
  };

  (void)xine;
  (void)data;

  tls_register_config_keys(xine->config);

  return (void *)&tls_gnutls_class;
}

/*
 * exported plugin catalog entry
 */

static const xine_module_info_t module_info_gnutls = {
  .priority = 10,
  .type     = "tls_v1",
};

const plugin_info_t xine_plugin_info[] EXPORTED = {
  /* type, API, "name", version, special_info, init_function */
  { PLUGIN_XINE_MODULE, 1, "gnutls", XINE_VERSION_CODE, &module_info_gnutls, gnutls_init_class },
  { PLUGIN_NONE, 0, NULL, 0, NULL, NULL }
};

