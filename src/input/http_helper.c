/*
 * Copyright (C) 2000-2003 the xine project
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
 * URL helper functions
 *
 * $Id: http_helper.c,v 1.1 2003/11/26 08:09:58 tmattern Exp $ 
 */
#include "xine_internal.h"
#include "http_helper.h"

static char *_strndup(const char *s, size_t n) {
  char *ret;
  
  ret = malloc (n + 1);
  strncpy(ret, s, n);
  ret[n] = '\0';
  return ret;
}

int _x_parse_url (char *url, char **proto, char** host, int *port,
                         char **user, char **password, char **uri) {
  char   *start      = NULL;
  char   *authcolon  = NULL;
  char	 *at         = NULL;
  char	 *portcolon  = NULL;
  char   *slash      = NULL;
  char   *end        = NULL;
  char   *strtol_err = NULL;

  if (!url)      abort();
  if (!proto)    abort();
  if (!user)     abort();
  if (!password) abort();
  if (!host)     abort();
  if (!port)     abort();
  if (!uri)      abort();

  *proto    = NULL;
  *port     = 0;
  *user     = NULL;
  *host     = NULL;
  *password = NULL;
  *uri      = NULL;

  /* proto */  
  start = strstr(url, "://");
  end  = start + strlen(start) - 1;
  if (!start || (start == url))
    goto error;
  
  *proto = _strndup(url, start - url);

  /* user:password */
  start += 3;
  at = strchr(start, '@');
  slash = strchr(start, '/');
  
  if (at && slash && (at > slash))
    at = NULL;
  
  if (at) {
    authcolon = strchr(start, ':');
    if(authcolon && authcolon < at) {
      *user = _strndup(start, authcolon - start);
      *password = _strndup(authcolon + 1, at - authcolon - 1);
      if ((authcolon == start) || (at == (authcolon + 1))) goto error;
    } else {
      /* no password */
      *user = _strndup(start, at - start);
      if (at == start) goto error;
    }
    start = at + 1;
  }

  /* host:port (ipv4) */
  /* [host]:port (ipv6) */
  if (*start != '[')
  {
    /* ipv4*/
    portcolon = strchr(start, ':');
    if (slash) {
      if (portcolon && portcolon < slash) {
        *host = _strndup(start, portcolon - start);
        if (portcolon == start) goto error;
        *port = strtol(portcolon + 1, &strtol_err, 10);
        if ((strtol_err != slash) || (strtol_err == portcolon + 1))
          goto error;
      } else {
        *host = _strndup(start, slash - start);
        if (slash == start) goto error;
      }
    } else {
      if (portcolon) {
        *host = _strndup(start, portcolon - start);
        if (portcolon < end) {
          *port = strtol(portcolon + 1, &strtol_err, 10);
          if (*strtol_err != '\0') goto error;
        } else {
          goto error;
        }
      } else {
        if (*start == '\0') goto error;
        *host = strdup(start);
      }
    }
  } else {
    /* ipv6*/
    char *hostendbracket;

    hostendbracket = strchr(start, ']');
    if (hostendbracket != NULL) {
      if (hostendbracket == start + 1) goto error;
      *host = _strndup(start + 1, hostendbracket - start - 1);

      if (hostendbracket < end) {
        /* Might have a trailing port */
        if (*(hostendbracket + 1) == ':') {
          portcolon = hostendbracket + 1;
          if (portcolon < end) {
            *port = strtol(portcolon + 1, &strtol_err, 10);
            if ((*strtol_err != '\0') && (*strtol_err != '/')) goto error;
          } else {
            goto error;
          }
        }
      }
    } else {
      goto error;
    }
  }

  /* uri */
  start = slash;
  if (start)
    *uri = strdup(start);
  else
    *uri = strdup("/");
  
  return 1;
  
error:
  if (*proto) {
    free (*proto);
    *proto = NULL;
  }
  if (*user) {
    free (*user);
    *user = NULL;
  }
  if (*password) {
    free (*password);
    *password = NULL;
  }
  if (*host) {
    free (*host);
    *host = NULL;
  }
  if (*port) {
    *port = 0;
  }
  if (*uri) {
    free (*uri);
    *uri = NULL;
  }
  return 0;  
}


#ifdef TEST_URL
/*
 * url parser test program
 */

static int check_url(char *url, int ok) {
  char *proto, *host, *user, *password, *uri;
  int port;
  int res;
  
  printf("--------------------------------\n");
  printf("url=%s\n", url);
  res = _x_parse_url (url,
                      &proto, &host, &port, &user, &password, &uri);
  if (res) {
    printf("proto=%s, host=%s, port=%d, user=%s, password=%s, uri=%s\n",
           proto, host, port, user, password, uri);
    free(proto);
    free(host);
    free(user);
    free(password);
    free(uri);
  } else {
    printf("bad url\n");
  }
  if (res == ok) {
    printf("test OK\n", url);
    return 1;
  } else {
    printf("test KO\n", url);
    return 0;
  }
}

int main(int argc, char** argv) {
  char *proto, host, port, user, password, uri;
  int res = 0;
  
  res += check_url("http://www.toto.com/test1.asx", 1);
  res += check_url("http://www.toto.com:8080/test2.asx", 1);
  res += check_url("http://titi:pass@www.toto.com:8080/test3.asx", 1);
  res += check_url("http://www.toto.com", 1);
  res += check_url("http://www.toto.com/", 1);
  res += check_url("http://www.toto.com:80", 1);
  res += check_url("http://www.toto.com:80/", 1);
  res += check_url("http://www.toto.com:", 0);
  res += check_url("http://www.toto.com:/", 0);
  res += check_url("http://www.toto.com:abc", 0);
  res += check_url("http://www.toto.com:abc/", 0);
  res += check_url("http://titi@www.toto.com:8080/test4.asx", 1);
  res += check_url("http://@www.toto.com:8080/test5.asx", 0);
  res += check_url("http://:@www.toto.com:8080/test6.asx", 0);
  res += check_url("http:///test6.asx", 0);
  res += check_url("http://:/test7.asx", 0);
  res += check_url("http://", 0);
  res += check_url("http://:", 0);
  res += check_url("http://@", 0);
  res += check_url("http://:@", 0);
  res += check_url("http://:/@", 0);
  res += check_url("http://www.toto.com:80a/", 0);
  res += check_url("http://[www.toto.com]", 1);
  res += check_url("http://[www.toto.com]/", 1);
  res += check_url("http://[www.toto.com]:80", 1);
  res += check_url("http://[www.toto.com]:80/", 1);
  res += check_url("http://[12:12]:80/", 1);
  res += check_url("http://user:pass@[12:12]:80/", 1);
  printf("================================\n");
  if (res != 28) {
    printf("result: KO\n");
  } else {
    printf("result: OK\n");
  }
}
#endif
