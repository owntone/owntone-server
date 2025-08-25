/*
 * Copyright (C) 2023 Espen Jurgensen
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 */

#ifdef HAVE_CONFIG_H
# include <config.h>
#endif

#include <stdlib.h>
#include <stdio.h> // fopen
#include <stdarg.h> // va_*
#include <string.h> // strlen
#include <ctype.h> // isspace

#include <libxml/parser.h>
#include <libxml/tree.h>

typedef xmlNode xml_node;

/* --------------------------------- Helpers -------------------------------- */

static char *
trim(char *str)
{
  char *term;

  if (!str)
    return NULL;

  while (isspace(*str))
    str++;

  term = (char *)str + strlen(str);
  while (term != str && isspace(*(term - 1)))
    term--;

  *term = '\0';

  return str;
}


/* -------------------------- Wrapper implementation ------------------------ */

char *
xml_to_string(xml_node *top, const char *xml_declaration)
{
  xmlBuffer *buf;
  const xmlChar *xml_string;
  char *s;

  buf = xmlBufferCreate();
  if (!buf)
    return NULL;

  if (xml_declaration)
    xmlBufferWriteChar(buf, xml_declaration);

  xmlNodeDump(buf, top->doc, top, 0, 0);

  xml_string = xmlBufferContent(buf);

  s = xml_string ? strdup((char *)xml_string) : NULL;

  xmlBufferFree(buf);

  return s;
}

// This works both for well-formed xml strings (beginning with <?xml..) and for
// those that get straight down to business (<foo...)
xml_node *
xml_from_string(const char *string)
{
  xmlDocPtr doc;

  doc = xmlReadMemory(string, strlen(string), NULL, NULL, XML_PARSE_NOBLANKS | XML_PARSE_NOCDATA | XML_PARSE_NONET);
  if (!doc)
    return NULL;

  return xmlDocGetRootElement(doc);
}

xml_node *
xml_from_file(const char *path)
{
  xmlDocPtr doc;

  doc = xmlReadFile(path, NULL, XML_PARSE_NOBLANKS | XML_PARSE_NOCDATA | XML_PARSE_NONET);
  if (!doc)
    return NULL;

  return xmlDocGetRootElement(doc);
}

void
xml_free(xml_node *top)
{
  xmlFreeDoc(top->doc);
}

xml_node *
xml_get_child(xml_node *top, const char *name)
{
  xml_node *cur;

  for (cur = xmlFirstElementChild(top); cur; cur = xmlNextElementSibling(cur))
    {
      if (xmlStrEqual(BAD_CAST name, cur->name))
        break;
    }

  return cur;
}

xml_node *
xml_get_next(xml_node *top, xml_node *node)
{
  return xmlNextElementSibling(node);
}

// We don't use xpath because I couldn't figure how to make it search in a node
// subtree instead of in the entire xmlDoc + it is more complex than the below.
// If the XML is <foo><bar>value</bar></foo> then both path = "foo/bar" and path
// = "bar" (so a path without the top element) will return "value".
xml_node *
xml_get_node(xml_node *top, const char *path)
{
  xml_node *node = top;
  char *path_cpy;
  char *needle;
  char *ptr;

  if (!top)
    return NULL;
  if (!path)
    return top;

  path_cpy = strdup(path);

  needle = strtok_r(path_cpy, "/", &ptr);
  if (!needle)
    node = NULL;
  else if (xmlStrEqual(BAD_CAST needle, node->name))
    needle = strtok_r(NULL, "/", &ptr); // Descend one level down the path

  while (node && needle)
    {
      node = xml_get_child(node, needle);
      needle = strtok_r(NULL, "/", &ptr);
    }

  free(path_cpy);

  return node;
}

// These variations will all give the same result:
//
// <foo>FOO FOO</foo><bar>\nBAR BAR \n</bar>
// <foo>FOO FOO</foo><bar><![CDATA[BAR BAR]]></bar>
// <foo>\nFOO FOO\n</foo><bar>\n<![CDATA[BAR BAR]]></bar>
const char *
xml_get_val(xml_node *top, const char *path)
{
  xml_node *node;

  node = xml_get_node(top, path);
  if (!node || !node->children)
    return NULL;

  return trim((char *)node->children->content);
}

const char *
xml_get_attr(xml_node *top, const char *path, const char *name)
{
  xml_node *node;
  xmlAttr *prop;

  node = xml_get_node(top, path);
  if (!node)
    return NULL;

  prop = xmlHasProp(node, BAD_CAST name);
  if (!prop || !prop->children)
    return NULL;

  return trim((char *)prop->children->content);
}

xml_node *
xml_new(void)
{
  xmlDoc *doc;

  doc = xmlNewDoc(BAD_CAST "1.0");
  if (!doc)
    return NULL;

  return xmlDocGetRootElement(doc);
}

xml_node *
xml_new_node(xml_node *parent, const char *name, const char *val)
{
  xml_node *node;
  xmlDoc *doc = NULL;

  doc = parent ? parent->doc : xmlNewDoc(BAD_CAST "1.0");
  if (!doc)
    goto error;

  node = xmlNewDocRawNode(doc, NULL, BAD_CAST name, BAD_CAST val);
  if (!node)
    return NULL;

  if (parent)
    xmlAddChild(parent, node);
  else
    xmlDocSetRootElement(doc, node);

  return node;

 error:
  if (!parent)
    xmlFreeDoc(doc);
  return NULL;
}

xml_node *
xml_new_node_textf(xml_node *parent, const char *name, const char *format, ...)
{
  char *s = NULL;
  va_list va;
  xml_node *node;
  int ret;

  va_start(va, format);
  ret = vasprintf(&s, format, va);
  va_end(va);

  if (ret < 0)
    return NULL;

  node = xml_new_node(parent, name, s);

  free(s);

  return node;
}

xml_node *
xml_new_text(xml_node *parent, const char *val)
{
  xml_node *node;

  if (!parent)
    return NULL;

  node = xmlNewDocText(parent->doc, BAD_CAST val);
  if (!node)
    return NULL;

  return xmlAddChild(parent, node);
}
