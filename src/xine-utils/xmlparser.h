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
 *  $Id: xmlparser.h,v 1.1 2002/05/01 19:41:55 guenter Exp $
 *
 */
#ifndef XML_PARSER_H
#define XML_PARSER_H

/* parser modes */
#define XML_PARSER_CASE_INSENSITIVE  0
#define XML_PARSER_CASE_SENSITIVE    1

/* return codes */
#define XML_PARSER_OK                0
#define XML_PARSER_ERROR             1


/* xml property */
typedef struct xml_property_s {
	char *name;
	char *value;
	struct xml_property_s *next;
} xml_property_t;

/* xml node */
typedef struct xml_node_s {
	char *name;
	char *data;
	struct xml_property_s *props;
	struct xml_node_s *child;
	struct xml_node_s *next;
} xml_node_t;

void xml_parser_init(char * buf, int size, int mode);

int xml_parser_build_tree(xml_node_t **root_node);

void xml_parser_free_tree(xml_node_t *root_node);


#endif
