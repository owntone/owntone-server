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
 *
 * About pipe.c
 * --------------
 * This module will read a PCM16 stream from a named pipe and write it to the
 * input buffer. The user may start/stop playback from a pipe by selecting it
 * through a client. If the user has configured pipe_autostart, then pipes in
 * the library will also be watched for data, and playback will start/stop
 * automatically.
 *
 * The module will also look for pipes with a .metadata suffix, and if found,
 * the metadata will be parsed and fed to the player. The metadata must be in
 * the format Shairport uses for this purpose.
 *
 */

#ifdef HAVE_CONFIG_H
# include <config.h>
#endif

#include <stdio.h> // fopen
#include <stdarg.h> // va_*
#include <ctype.h>

#include <mxml.h>

typedef mxml_node_t xml_node;


/* ---------------- Compability with older versions of mxml ----------------- */

// mxml 2.10 has a memory leak in mxmlDelete, see https://github.com/michaelrsweet/mxml/issues/183
// - and since this is the version in Ubuntu 18.04 LTS and Raspian Stretch, we
// fix it by including a fixed mxmlDelete here. It should be removed once the
// major distros no longer have 2.10. The below code is msweet's fixed mxml.
#if (MXML_MAJOR_VERSION == 2) && (MXML_MINOR_VERSION <= 10)

#define mxmlDelete compat_mxmlDelete

static void
compat_mxml_free(mxml_node_t *node)
{
  int i;

  switch (node->type)
  {
    case MXML_ELEMENT :
        if (node->value.element.name)
	  free(node->value.element.name);

	if (node->value.element.num_attrs)
	{
	  for (i = 0; i < node->value.element.num_attrs; i ++)
	  {
	    if (node->value.element.attrs[i].name)
	      free(node->value.element.attrs[i].name);
	    if (node->value.element.attrs[i].value)
	      free(node->value.element.attrs[i].value);
	  }

          free(node->value.element.attrs);
	}
        break;
    case MXML_INTEGER :
        break;
    case MXML_OPAQUE :
        if (node->value.opaque)
	  free(node->value.opaque);
        break;
    case MXML_REAL :
        break;
    case MXML_TEXT :
        if (node->value.text.string)
	  free(node->value.text.string);
        break;
    case MXML_CUSTOM :
        if (node->value.custom.data &&
	    node->value.custom.destroy)
	  (*(node->value.custom.destroy))(node->value.custom.data);
	break;
    default :
        break;
  }

  free(node);
}

__attribute__((unused)) static void
compat_mxmlDelete(mxml_node_t *node)
{
  mxml_node_t	*current,
		*next;

  if (!node)
    return;

  mxmlRemove(node);
  for (current = node->child; current; current = next)
  {
    if ((next = current->child) != NULL)
    {
      current->child = NULL;
      continue;
    }

    if ((next = current->next) == NULL)
    {
      if ((next = current->parent) == node)
        next = NULL;
    }
    compat_mxml_free(current);
  }

  compat_mxml_free(node);
}
#endif

/* For compability with mxml 2.6 */
#ifndef HAVE_MXMLGETTEXT
__attribute__((unused)) static const char *			/* O - Text string or NULL */
mxmlGetText(mxml_node_t *node,		/* I - Node to get */
            int         *whitespace)	/* O - 1 if string is preceded by whitespace, 0 otherwise */
{
  if (node->type == MXML_TEXT)
    return (node->value.text.string);
  else if (node->type == MXML_ELEMENT &&
           node->child &&
	   node->child->type == MXML_TEXT)
    return (node->child->value.text.string);
  else
    return (NULL);
}
#endif

#ifndef HAVE_MXMLGETOPAQUE
__attribute__((unused)) static const char *			/* O - Opaque string or NULL */
mxmlGetOpaque(mxml_node_t *node)	/* I - Node to get */
{
  if (!node)
    return (NULL);

  if (node->type == MXML_OPAQUE)
    return (node->value.opaque);
  else if (node->type == MXML_ELEMENT &&
           node->child &&
	   node->child->type == MXML_OPAQUE)
    return (node->child->value.opaque);
  else
    return (NULL);
}
#endif

#ifndef HAVE_MXMLGETFIRSTCHILD
__attribute__((unused)) static mxml_node_t *			/* O - First child or NULL */
mxmlGetFirstChild(mxml_node_t *node)	/* I - Node to get */
{
  if (!node || node->type != MXML_ELEMENT)
    return (NULL);

  return (node->child);
}
#endif

#ifndef HAVE_MXMLGETTYPE
__attribute__((unused)) static mxml_type_t			/* O - Type of node */
mxmlGetType(mxml_node_t *node)		/* I - Node to get */
{
  return (node->type);
}
#endif


/* --------------------------------- Helpers -------------------------------- */

// We get values from mxml via GetOpaque, but that means they can whitespace,
// thus we trim them. A bit dirty, since the values are in principle const.
static const char *
trim(const char *str)
{
  char *term;

  if (!str)
    return NULL;

  while (isspace(*str))
    str++;

  term = (char *)str + strlen(str);
  while (term != str && isspace(*(term - 1)))
    term--;

  // Dirty write to the const string from mxml
  *term = '\0';

  return str;
}


/* -------------------------- Wrapper implementation ------------------------ */

char *
xml_to_string(xml_node *top)
{
  return mxmlSaveAllocString(top, MXML_NO_CALLBACK);
}

// This works both for well-formed xml strings (beginning with <?xml..) and for
// those that get straight down to business (<foo...)
xml_node *
xml_from_string(const char *string)
{
  mxml_node_t *top;
  mxml_node_t *node;

  top = mxmlNewXML("1.0");
  if (!top)
    goto error;

  node = mxmlLoadString(top, string, MXML_OPAQUE_CALLBACK);
  if (!node)
    goto error;

  return top;

 error:
  mxmlDelete(top);
  return NULL;
}

xml_node *
xml_from_file(const char *path)
{
  FILE *fp;
  mxml_node_t *top;
  mxml_node_t *node;

  top = mxmlNewXML("1.0");
  if (!top)
    goto error;

  fp = fopen(path, "r");
  node = mxmlLoadFile(top, fp, MXML_OPAQUE_CALLBACK);
  fclose(fp);

  if (!node)
    goto error;

  return top;

 error:
  mxmlDelete(top);
  return NULL;
}

void
xml_free(xml_node *top)
{
  mxmlDelete(top);
}

xml_node *
xml_get_node(xml_node *top, const char *path)
{
  mxml_node_t *node;
  mxml_type_t type;

  // This example shows why we can't just return the result of mxmlFindPath:
  // <?xml version="1.0""?><rss>
  //	<channel>
  //		<title><![CDATA[Tissages]]></title>
  // mxmlFindPath(top, "rss/channel") will return an OPAQUE node where the
  // opaque value is just the whitespace. What we want is the ELEMENT parent,
  // because that's the one we can use to search for children nodes ("title").
  node = mxmlFindPath(top, path);
  type = mxmlGetType(node);
  if (type == MXML_ELEMENT)
    return node;

  return mxmlGetParent(node);
}

xml_node *
xml_get_next(xml_node *top, xml_node *node)
{
  const char *name;
  const char *s;

  name = mxmlGetElement(node);
  if (!name)
    return NULL;

  while ( (node = mxmlGetNextSibling(node)) )
    {
      s = mxmlGetElement(node);
      if (s && strcmp(s, name) == 0)
	return node;
    }

  return NULL;
}

// Walks through the children of the "path" node until it finds one that is
// not just whitespace and returns a trimmed value (except for CDATA). Means
// that these variations will all give the same result:
//
// <foo>FOO FOO</foo><bar>\nBAR BAR \n</bar>
// <foo>FOO FOO</foo><bar><![CDATA[BAR BAR]]></bar>
// <foo>\nFOO FOO\n</foo><bar>\n<![CDATA[BAR BAR]]></bar>
const char *
xml_get_val(xml_node *top, const char *path)
{
  mxml_node_t *parent;
  mxml_node_t *node;
  mxml_type_t type;
  const char *s = "";

  parent = xml_get_node(top, path);
  if (!parent)
    return NULL;

  for (node = mxmlGetFirstChild(parent); node; node = mxmlGetNextSibling(node))
    {
      type = mxmlGetType(node);
      if (type == MXML_OPAQUE)
	s = trim(mxmlGetOpaque(node));
      else if (type == MXML_ELEMENT)
        s = mxmlGetCDATA(node);

      if (s && *s != '\0')
	break;
    }

  return s;
}

const char *
xml_get_attr(xml_node *top, const char *path, const char *name)
{
  mxml_node_t *node = mxmlFindPath(top, path);

  return mxmlElementGetAttr(node, name);
}

xml_node *
xml_new_node(xml_node *parent, const char *name, const char *val)
{
  if (!parent)
    parent = MXML_NO_PARENT;

  mxml_node_t *node = mxmlNewElement(parent, name);
  if (!val)
    return node; // We're done, caller gets an ELEMENT to use as parent

  mxmlNewText(node, 0, val);
  return node;
}

xml_node *
xml_new_node_textf(xml_node *parent, const char *name, const char *format, ...)
{
  char *s = NULL;
  va_list va;
  mxml_node_t *node;
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

void
xml_new_text(xml_node *parent, const char *val)
{
  mxmlNewText(parent, 0, val);
}
