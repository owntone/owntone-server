#ifndef __MXML_COMPAT_H__
#define __MXML_COMPAT_H__

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
