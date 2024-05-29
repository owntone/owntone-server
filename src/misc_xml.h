#ifndef SRC_MISC_XML_H_
#define SRC_MISC_XML_H_

// This wraps libxml2 and adds some convenience functions

typedef void xml_node;

char *
xml_to_string(xml_node *top, const char *xml_declaration);

xml_node *
xml_from_string(const char *string);

xml_node *
xml_from_file(const char *path);

void
xml_free(xml_node *top);

xml_node *
xml_get_node(xml_node *top, const char *path);

// Only returns sibling nodes that have the same name as input node
xml_node *
xml_get_next(xml_node *top, xml_node *node);

const char *
xml_get_val(xml_node *top, const char *path);

const char *
xml_get_attr(xml_node *top, const char *path, const char *name);

// Will create a new XML document with the node as root if parent is NULL
xml_node *
xml_new_node(xml_node *parent, const char *name, const char *val);

xml_node *
xml_new_node_textf(xml_node *parent, const char *name, const char *format, ...);

// Adds a text node to parent, which must be an element node
void
xml_new_text(xml_node *parent, const char *val);

#endif /* SRC_MISC_XML_H_ */
