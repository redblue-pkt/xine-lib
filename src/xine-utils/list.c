/* 
 * Copyright (C) 2000-2002 the xine project
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
 * $Id: list.c,v 1.4 2002/10/24 15:01:18 jkeil Exp $
 *
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <inttypes.h>

#include "xineutils.h"

/*
 * create a new, empty list
 */
xine_list_t *xine_list_new (void) {
  xine_list_t *list;

  list = (xine_list_t *) xine_xmalloc(sizeof(xine_list_t));

  list->first=NULL;
  list->last =NULL;
  list->cur  =NULL;

  return list;
}

/*
 * dispose a list (and only the list, contents have to be managed separately)
 *                 TODO: this is easy to fix by using "content destructors"  
 */
void xine_list_free(xine_list_t *l) {
  xine_node_t *node;

  if (!l) {
    fprintf(stderr, "%s(): No list.\n", __XINE_FUNCTION__);
    return;
  }
 
  if(!l->first) {
    return;
  }

  node = l->first;
  
  while(node) {
    xine_node_t *n = node;
    
    /* TODO: support for content destructors */
    node = n->next;
    free(n);
  }
  
  l->first = l->cur = l->last = NULL; /* FIXME: free(l) instead */
}

void *xine_list_first_content (xine_list_t *l) {

  l->cur = l->first;

  if (l->first) 
    return l->first->content;
  else
    return NULL;
}

void *xine_list_next_content (xine_list_t *l) {
  if (l->cur) {

    if (l->cur->next) {
      l->cur = l->cur->next;
      return l->cur->content;
    } 
    else
      return NULL;
    
  } 
  else {
    fprintf(stderr,"%s() WARNING: passed end of list\n", __XINE_FUNCTION__);
    return NULL;
  }    
}

int xine_list_is_empty (xine_list_t *l) {

  if (l == NULL){
	fprintf(stderr, "%s(): list is NULL\n", __XINE_FUNCTION__);
	return -1;
  }
  return (l->first != NULL);
}

void *xine_list_last_content (xine_list_t *l) {

  if (l->last) {
    l->cur = l->last;
    return l->last->content;
  } 
  else {
    fprintf(stderr, "xine_list: wanted last of empty list\n");
    return NULL;
  }    
}

void *xine_list_prev_content (xine_list_t *l) {

  if (l->cur) {
    if (l->cur->prev) {
      l->cur = l->cur->prev;
      return l->cur->content;
    } 
    else
      return NULL;
  } 
  else {
    fprintf(stderr, "xine_list: passed begin of list\n");
    return NULL;
  }    
}

void xine_list_append_priority_content (xine_list_t *l, void *content, int priority) {
  xine_node_t *node;

  node = (xine_node_t *) xine_xmalloc(sizeof(xine_node_t));
  node->content = content;
  node->priority = priority;

  if (l->first) {
    xine_node_t *cur;

    cur = l->first;

    while(1) {
      if( priority >= cur->priority ) {
        node->next = cur;
        node->prev = cur->prev;

        if( node->prev )
          node->prev->next = node;
        else
          l->first = node;
        cur->prev = node;

        l->cur = node;
        break;
      }

      if( !cur->next ) {
        node->next = NULL;
        node->prev = cur;
        cur->next = node;

        l->cur = node;
        l->last = node;
        break;
      }
     
      cur = cur->next;
    }
  } 
  else {
    l->first = l->last = l->cur = node;
    node->prev = node->next = NULL;
  }
}


void xine_list_append_content (xine_list_t *l, void *content) {
  xine_node_t *node;

  node = (xine_node_t *) xine_xmalloc(sizeof(xine_node_t));
  node->content = content;

  if (l->last) {
    node->next = NULL;
    node->prev = l->last;
    l->last->next = node;
    l->last = node;
    l->cur = node;
  } 
  else {
    l->first = l->last = l->cur = node;
    node->prev = node->next = NULL;
  }
}

void xine_list_insert_content (xine_list_t *l, void *content) {
  xine_node_t *nodecur, *nodenext, *nodenew;
  
  if(l->cur->next) {
    nodenew = (xine_node_t *) xine_xmalloc(sizeof(xine_node_t));

    nodenew->content = content;
    nodecur = l->cur;
    nodenext = l->cur->next;
    nodecur->next = nodenew;
    nodenext->prev = nodenew;
    nodenew->prev = nodecur;
    nodenew->next = nodenext;
    l->cur = nodenew;
  }
  else { /* current is last, append to the list */
    xine_list_append_content(l, content);
  }

}

void xine_list_delete_current (xine_list_t *l) {
  xine_node_t *node_cur;

  node_cur = l->cur;

  if(node_cur->prev) {
    node_cur->prev->next = node_cur->next;
  } 
  else { /* First entry */
    l->first = node_cur->next;
  }
  
  if(node_cur->next) {
    node_cur->next->prev = node_cur->prev;
    l->cur = node_cur->next;
  }
  else { /* last entry in the list */
    l->last = node_cur->prev;
    l->cur = node_cur->prev;
  }

  /* TODO:  support content destructors */
  free(node_cur);
}
