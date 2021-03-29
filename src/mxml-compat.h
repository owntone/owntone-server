#ifndef __MXML_COMPAT_H__
#define __MXML_COMPAT_H__

#include <mxml.h>

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

// Debian 10.x amd64 w/mxml 2.12 has a mxmlNewTextf that causes segfault when
// mxmlSaveString or mxmlSaveAllocString is called,
// ref https://github.com/owntone/owntone-server/issues/938
#if (MXML_MAJOR_VERSION == 2) && (MXML_MINOR_VERSION == 12)

#include <stdarg.h>

#define mxmlNewTextf compat_mxmlNewTextf

__attribute__((unused)) static mxml_node_t *
compat_mxmlNewTextf(mxml_node_t *parent, int whitespace, const char *format, ...)
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

  node = mxmlNewText(parent, whitespace, s);

  free(s);

  return node;
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

#endif /* !__MXML_COMPAT_H__ */
