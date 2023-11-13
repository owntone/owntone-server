#ifndef SRC_MISC_XML_H_
#define SRC_MISC_XML_H_

// This wraps mxml and adds some convenience functions. This also means that
// callers don't need to concern themselves with changes and bugs in various
// versions of mxml.

typedef void xml_node;

// Wraps mxmlSaveAllocString. Returns NULL on error.
char *
xml_to_string(xml_node *top);

// Wraps mxmlNewXML and mxmlLoadString, so creates an xml struct with the parsed
// content of string. Returns NULL on error.
xml_node *
xml_from_string(const char *string);

// Wraps mxmlNewXML and mxmlLoadFile, so creates an xml struct with the parsed
// content of string. Returns NULL on error.
xml_node *
xml_from_file(const char *path);

// Wraps mxmlDelete, which will free node + underlying nodes
void
xml_free(xml_node *top);

// Wraps mxmlFindPath.
xml_node *
xml_get_node(xml_node *top, const char *path);

// Wraps mxmlGetNextSibling, but only returns sibling nodes that have the same
// name as input node.
xml_node *
xml_get_next(xml_node *top, xml_node *node);

// Wraps mxmlFindPath and mxmlGetOpaque + mxmlGetCDATA. Returns NULL if nothing
// can be found.
const char *
xml_get_val(xml_node *top, const char *path);

// Wraps mxmlFindPath and mxmlElementGetAttr. Returns NULL if nothing can be
// found.
const char *
xml_get_attr(xml_node *top, const char *path, const char *name);

xml_node *
xml_new_node(xml_node *parent, const char *name, const char *val);

xml_node *
xml_new_node_textf(xml_node *parent, const char *name, const char *format, ...);

void
xml_new_text(xml_node *parent, const char *val);

#endif /* SRC_MISC_XML_H_ */
