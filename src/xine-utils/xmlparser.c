/*
 *  Copyright (C) 2002 the xine project
 *
 *  This file is part of xine, a free video player.
 *
 *  xine is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  xine is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA
 *
 *  $Id: xmlparser.c,v 1.3 2002/12/02 22:37:08 f1rmb Exp $
 *
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "xmllexer.h"
#include "xmlparser.h"


#define TOKEN_SIZE  4 * 1024
#define DATA_SIZE   4 * 1024
#define MAX_RECURSION 10

/* #define LOG */

/* private global variables */
int xml_parser_mode;

/* private functions */

char * strtoupper(char * str) {
  int i = 0;

  while (str[i] != '\0') {
    str[i] = (char)toupper((int)str[i]);
    i++;
  }
  return str;
}

xml_node_t * new_xml_node() {
  xml_node_t * new_node;

  new_node = (xml_node_t*) malloc(sizeof(xml_node_t));
  new_node->name  = NULL;
  new_node->data  = NULL;
  new_node->props = NULL;
  new_node->child = NULL;
  new_node->next  = NULL;
  return new_node;
}

void free_xml_node(xml_node_t * node) {
  free(node);
}

xml_property_t * new_xml_property() {
  xml_property_t * new_property;

  new_property = (xml_property_t*) malloc(sizeof(xml_property_t));
  new_property->name  = NULL;
  new_property->value = NULL;
  new_property->next  = NULL;
  return new_property;
}

void free_xml_property(xml_property_t * property) {
  free(property);
}

void xml_parser_init(char * buf, int size, int mode) {
  lexer_init(buf, size);
  xml_parser_mode = mode;
}

void xml_parser_free_props(xml_property_t *current_property) {
  if (current_property) {
    if (!current_property->next) {
      free_xml_property(current_property);
    } else {
      xml_parser_free_props(current_property->next);
      free_xml_property(current_property);
    }
  }
}

void xml_parser_free_tree(xml_node_t *current_node) {
  if (current_node) {
    /* propertys */
    if (current_node->props) {
      xml_parser_free_props(current_node->props);
    }

    /* child nodes */
    if (current_node->child) {
      xml_parser_free_tree(current_node->child);
    }

    if (current_node->next) {
      xml_parser_free_tree(current_node->next);
    }

    free_xml_node(current_node);
  }
}

int xml_parser_get_node(xml_node_t *current_node, char *root_name, int rec) {
  char tok[TOKEN_SIZE];
  char property_name[TOKEN_SIZE];
  char node_name[TOKEN_SIZE];
  int state = 0;
  int res = 0;
  int parse_res;
  int bypass_get_token = 0;
  xml_node_t *subtree = NULL;
  xml_node_t *current_subtree = NULL;
  xml_property_t *current_property = NULL;
  xml_property_t *propertys = NULL;

  if (rec < MAX_RECURSION) {

    while ((bypass_get_token) || (res = lexer_get_token(tok, TOKEN_SIZE)) != T_ERROR) {
      bypass_get_token = 0;
#ifdef LOG
      printf("xmlparser: info: %d - %d : %s\n", state, res, tok);
#endif
      switch (state) {
      case 0:
	switch (res) {
	case (T_EOL):
	case (T_SEPAR):
	  /* do nothing */
	  break;
	case (T_EOF):
	  return 0; /* normal end */
	  break;
	case (T_M_START_1):
	  state = 1;
	  break;
	case (T_M_START_2):
	  state = 3;
	  break;
	case (T_C_START):
	  state = 7;
	  break;
	case (T_TI_START):
	  state = 8;
	  break;
	case (T_DOCTYPE_START):
	  state = 9;
	  break;
	case (T_DATA):
	  /* current data */
	  current_node->data = (char *)malloc(strlen(tok) + 1);
	  strcpy(current_node->data, tok);
#ifdef LOG
	  printf("xmlparser: info: node data : %s\n", current_node->data);
#endif
	  break;
	default:
	  printf("xmlparser: error: unexpected token \"%s\", state %d\n", tok, state);
	  return -1;
	  break;
	}
	break;

      case 1:
	switch (res) {
	case (T_IDENT):
	  propertys = NULL;
	  current_property = NULL;

	  /* save node name */
	  if (xml_parser_mode == XML_PARSER_CASE_INSENSITIVE) {
	    strtoupper(tok);
	  }
	  strcpy(node_name, tok);
	  state = 2;
#ifdef LOG
	  printf("xmlparser: info: current node name \"%s\"\n", node_name);
#endif
	  break;
	default:
	  printf("xmlparser: error: unexpected token \"%s\", state %d\n", tok, state);
	  return -1;
	  break;
	}
	break;
      case 2:
	switch (res) {
	case (T_EOL):
	case (T_SEPAR):
	  /* nothing */
	  break;
	case (T_M_STOP_1):
	  /* new subtree */
	  subtree = new_xml_node();

	  /* set node name */
	  subtree->name = malloc(strlen(node_name + 1));
	  strcpy(subtree->name, node_name);

	  /* set node propertys */
	  subtree->props = propertys;
#ifdef LOG
	  printf("xmlparser: info: recursive level: %d\n", rec);
	  printf("xmlparser: info: new subtree %s\n", node_name);
#endif
	  parse_res = xml_parser_get_node(subtree, node_name, rec + 1);
#ifdef LOG
	  printf("xmlparser: info: new subtree result: %d\n", parse_res);
	  printf("xmlparser: info: recursive level: %d\n", rec);
#endif
	  if (parse_res != 0) {
	    return parse_res;
	  }
	  if (current_subtree == NULL) {
	    current_node->child = subtree;
	    current_subtree = subtree;
	  } else {
	    current_subtree->next = subtree;
	    current_subtree = subtree;
	  }
	  state = 0;
	  break;
	case (T_M_STOP_2):
	  /* new leaf */
	  /* new subtree */
	  subtree = new_xml_node();

	  /* set node name */
	  subtree->name = malloc(strlen(node_name + 1));
	  strcpy(subtree->name, node_name);

	  /* set node propertys */
	  subtree->props = propertys;

	  if (current_subtree == NULL) {
	    current_node->child = subtree;
	    current_subtree = subtree;
	  } else {
	    current_subtree->next = subtree;
	    current_subtree = subtree;
	  }
	  state = 0;
	  break;
	case (T_IDENT):
	  /* save property name */
	  if (xml_parser_mode == XML_PARSER_CASE_INSENSITIVE) {
	    strtoupper(tok);
	  }
	  strcpy(property_name, tok);
	  state = 5;
#ifdef LOG
	  printf("xmlparser: info: current property name \"%s\"\n", property_name);
#endif
	  break;
	default:
	  printf("xmlparser: error: unexpected token \"%s\", state %d\n", tok, state);
	  return -1;
	  break;
	}
	break;

      case 3:
	switch (res) {
	case (T_IDENT):
	  /* must be equal to root_name */
	  if (xml_parser_mode == XML_PARSER_CASE_INSENSITIVE) {
	    strtoupper(tok);
	  }
	  if (strcmp(tok, root_name) == 0) {
	    state = 4;
	  } else {
	    printf("xmlparser: error: xml struct, tok=%s, waited_tok=%s\n", tok, root_name);
	    return -1;
	  }
	  break;
	default:
	  printf("xmlparser: error: unexpected token \"%s\", state %d\n", tok, state);
	  return -1;
	  break;
	}
	break;

				/* > expected */
      case 4:
	switch (res) {
	case (T_M_STOP_1):
	  return 0;
	  break;
	default:
	  printf("xmlparser: error: unexpected token \"%s\", state %d\n", tok, state);
	  return -1;
	  break;
	}
	break;

				/* = or > or ident or separator expected */
      case 5:
	switch (res) {
	case (T_EOL):
	case (T_SEPAR):
	  /* do nothing */
	  break;
	case (T_EQUAL):
	  state = 6;
	  break;
	case (T_IDENT):
	  bypass_get_token = 1; /* jump to state 2 without get a new token */
	  state = 2;
	  break;
	case (T_M_STOP_1):
	  /* add a new property without value */
	  if (current_property == NULL) {
	    propertys = new_xml_property();
	    current_property = propertys;
	  } else {
	    current_property->next = new_xml_property();
	    current_property = current_property->next;
	  }
	  current_property->name = (char *)malloc(strlen(property_name) + 1);
	  strcpy(current_property->name, property_name);
#ifdef LOG
	  printf("xmlparser: info: new property %s\n", current_property->name);
#endif
	  bypass_get_token = 1; /* jump to state 2 without get a new token */
	  state = 2;
	  break;
	default:
	  printf("xmlparser: error: unexpected token \"%s\", state %d\n", tok, state);
	  return -1;
	  break;
	}
	break;

				/* string or ident or separator expected */
      case 6:
	switch (res) {
	case (T_EOL):
	case (T_SEPAR):
	  /* do nothing */
	  break;
	case (T_STRING):
	case (T_IDENT):
	  /* add a new property */
	  if (current_property == NULL) {
	    propertys = new_xml_property();
	    current_property = propertys;
	  } else {
	    current_property->next = new_xml_property();
	    current_property = current_property->next;
	  }
	  current_property->name = (char *)malloc(strlen(property_name) + 1);
	  strcpy(current_property->name, property_name);
	  current_property->value = (char *)malloc(strlen(tok) + 1);
	  strcpy(current_property->value, tok);
#ifdef LOG
	  printf("xmlparser: info: new property %s=%s\n", current_property->name, current_property->value);
#endif
	  state = 2;
	  break;
	default:
	  printf("xmlparser: error: unexpected token \"%s\", state %d\n", tok, state);
	  return -1;
	  break;
	}
	break;

				/* --> expected */
      case 7:
	switch (res) {
	case (T_C_STOP):
	  state = 0;
	  break;
	default:
	  state = 7;
	  break;
	}
	break;

				/* > expected */
      case 8:
	switch (res) {
	case (T_TI_STOP):
	  state = 0;
	  break;
	default:
	  state = 8;
	  break;
	}
	break;

				/* ?> expected */
      case 9:
	switch (res) {
	case (T_M_STOP_1):
	  state = 0;
	  break;
	default:
	  state = 9;
	  break;
	}
	break;


      default:
	printf("xmlparser: error: unknown parser state, state=%d\n", state);
	return -1;
      }
    }
    /* lex error */
    printf("xmlparser: error: lexer error\n");
    return -1;
  } else {
    /* max recursion */
    printf("xmlparser: error: max recursion\n");
    return -1;
  }
}

int xml_parser_build_tree(xml_node_t **root_node) {
  xml_node_t *tmp_node;
  int res;

  *root_node = new_xml_node();
  tmp_node = new_xml_node();
  res = xml_parser_get_node(tmp_node, "", 0);
  if ((tmp_node->child) && (!tmp_node->child->next)) {
    (*root_node)->name  = tmp_node->child->name;
    (*root_node)->data  = tmp_node->child->data;
    (*root_node)->props = tmp_node->child->props;
    (*root_node)->child = tmp_node->child->child;
    (*root_node)->next  = tmp_node->child->next;
  } else {
    printf("xmlparser: error: xml struct\n");
    res = -1;
  }
  free(tmp_node);
  return res;
}
