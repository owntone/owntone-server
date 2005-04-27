/* ezxml.c
 *
 * Copyright 2005 Aaron Voisine <aaron@voisine.org>
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice shall be included
 * in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
 * IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
 * CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 * TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
 * SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <ctype.h>
#include <unistd.h>
#include <sys/types.h>
#ifndef EZXML_NOMMAP
#include <sys/mman.h>
#endif // EZXML_NOMMAP
#include <sys/stat.h>
#include "ezxml.h"

#define EZXML_TXTM 0x80      // flag value meaning txt was malloced
#define EZXML_WS   "\t\r\n " // whitespace

typedef struct ezxml_root *ezxml_root_t;
struct ezxml_root {   // additional data for the root tag
    struct ezxml xml; // is a super-struct built on top of ezxml struct
    ezxml_t cur;      // current xml tree insertion point
    void *m;          // original xml string
    size_t len;       // length of allocated memory for mmap, or -1 for malloc
    void *u;          // UTF-8 conversion of string if original was UTF-16
    const char *err;  // error string
    char ***pi;       // processing instructions
};

const char *EZXML_NOATTR[] = { NULL }; // empty tag attribute list

// called when parser finds close of tag
#define ezxml_close_tag(root) root->cur = root->cur->parent;

// returns the first child tag with the given name or NULL if not found
ezxml_t ezxml_child(ezxml_t xml, const char *name)
{
    xml = (xml) ? xml->child : NULL;
    while (xml && strcmp(name, xml->name)) xml = xml->sibling;
    return xml;
}

// returns the Nth tag with the same name in the same subsection or NULL if not
// found
ezxml_t ezxml_idx(ezxml_t xml, int idx)
{
    for (; xml && idx; idx--) xml = xml->next;
    return xml;
}

// returns the value of the requested tag attribute or NULL if not found
const char *ezxml_attr(ezxml_t xml, const char *attr)
{
    int i = 0;

    if (! xml) return NULL;
    while (xml->attr[i] && strcmp(attr, xml->attr[i])) i += 2;
    return (xml->attr[i]) ? xml->attr[i + 1] : NULL;
}

// same as ezxml_get but takes an alredy initialized va_list
ezxml_t ezxml_vget(ezxml_t xml, va_list ap)
{
    char *name = va_arg(ap, char *);
    int idx = -1;

    if (name && *name) {
        idx = va_arg(ap, int);    
        xml = ezxml_child(xml, name);
    }

    return (idx < 0) ? xml : ezxml_vget(ezxml_idx(xml, idx), ap);
}

// Traverses the xml tree to retrive a specific subtag. Takes a variable
// length list of tag names and indexes. The argument list must be terminated
// by either an index of -1 or an empty string tag name. Example: 
// title = ezxml_get(library, "shelf", 0, "book", 2, "title", -1);
// This retrieves the title of the 3rd book on the 1st shelf of library.
// Returns NULL if not found.
ezxml_t ezxml_get(ezxml_t xml, ...)
{
    va_list ap;
    ezxml_t ret;

    va_start(ap, xml);
    ret = ezxml_vget(xml, ap);
    va_end(ap);
    return ret;
}

// returns a NULL terminated array of processing instructions for the given
// target
const char **ezxml_pi(ezxml_t xml, const char *target)
{
    static const char *nopi = NULL;
    ezxml_root_t root;
    int i = 0;

    while (xml->parent) xml = xml->parent;
    root = (ezxml_root_t)xml;

    if (! root->pi) return &nopi;
    while (root->pi[i] && strcmp(target, root->pi[i][0])) i++;
    return (root->pi[i]) ? (const char **)root->pi[i] + 1 : &nopi;
}

// Converts \r or \r\n to a single \n. If decode is non-zero, decodes ampersand
// sequences in place. Returns s.
char *ezxml_decode(char *s, int decode)
{
    int b;
    char *e, *ret = s;
    long c, d;

    for (;;) {
        while (*s && *s != '\r' && *s != '&') s++;

        if (! *s) return ret;
        else if (*s == '\r') {
            *(s++) = '\n';
            if (*s == '\n') memmove(s, (s + 1), strlen(s));
            continue;
        }
        else if (! decode) { s++; continue; }
        else if (! strncmp(s, "&lt;", 4)) *(s++) = '<';
        else if (! strncmp(s, "&gt;", 4)) *(s++) = '>';
        else if (! strncmp(s, "&quot;", 6)) *(s++) = '"';
        else if (! strncmp(s, "&apos;", 6)) *(s++) = '\'';
        else if (! strncmp(s, "&amp;", 5)) s++;
        else if (! strncmp(s, "&#", 2)) {
            if (s[2] == 'x') c = strtol(s + 3, &e, 16); // base 16
            else c = strtol(s + 2, &e, 10); // base 10
            if (! c || *e != ';') { s++; continue; } // not a &#nnn; sequence

            if (c < 0x80) *(s++) = c; // US-ASCII subset
            else { // multi-byte UTF-8 sequence
                for (b = 0, d = c; d; d /= 2) b++; // number of bits in c
                b = (b - 2) / 5; // number of bytes in payload
                *(s++) = (0xFF << (7 - b)) | (c >> (6 * b)); // head
                while (b) *(s++) = 0x80 | ((c >> (6 * --b)) & 0x3F); // payload
            }
        }
        else { s++; continue; }
        
        memmove(s, strchr(s, ';') + 1, strlen(strchr(s, ';')));
    }    
}

// called when parser finds start of new tag
void ezxml_open_tag(ezxml_root_t root, char *name, char **attr)
{
    ezxml_t xml = root->cur;
    
    if (xml->name) xml = ezxml_add_child(xml, name, strlen(xml->txt));
    else xml->name = name;

    xml->attr = (const char **)attr;
    root->cur = xml;
}

// called when parser finds character content between open and closing tag
void ezxml_char_content(ezxml_root_t root, char *s, size_t len, short decode)
{
    ezxml_t xml = root->cur;
    size_t l;

    if (! xml || ! xml->name || ! len) return;

    s[len] = '\0';
    ezxml_decode(s, decode);

    if (! *(xml->txt)) xml->txt = s;
    else { // allocate our own memory and make a copy
        l = strlen(xml->txt);
        if (! (xml->flags & EZXML_TXTM)) {
            xml->txt = strcpy(malloc(l + len + 1), xml->txt);
            xml->flags |= EZXML_TXTM;
        }
        else xml->txt = realloc(xml->txt, l + len + 1);
        strcpy(xml->txt + l, s);
    }
}

// called when the parser finds an xml processing instruction
void ezxml_proc_inst(ezxml_root_t root, char *s, size_t len)
{
    int i = 0, j = 1;
    char *target = s;

    s[len] = '\0'; // null terminate instruction
    *(s += strcspn(s, EZXML_WS)) = '\0'; // null terminate target
    s += strspn(s + 1, EZXML_WS) + 1; // skip whitespace after target

    if (! root->pi) *(root->pi = malloc(sizeof(char**))) = NULL;

    while (root->pi[i] && strcmp(target, root->pi[i][0])) i++;
    if (! root->pi[i]) { // new target
        root->pi = realloc(root->pi, sizeof(char **) * (i + 2));
        root->pi[i] = malloc(sizeof(char *) * 2);
        root->pi[i][0] = target;
        root->pi[i][1] = (char *)(root->pi[i + 1] = NULL); // terminate pi list
    }

    while (root->pi[i][j]) j++;
    root->pi[i] = realloc(root->pi[i], sizeof(char *) * (j + 2));
    root->pi[i][j] = s;
    root->pi[i][j + 1] = NULL;
}

// set an error string and return root
ezxml_t ezxml_seterr(ezxml_root_t root, const char *err)
{
    root->err = err;
    return (ezxml_t)root;
}

// converts a UTF-16 string to UTF-8, returns a new string the must be freed or
// NULL if no conversion was needed
char *ezxml_to_utf8(char **s, size_t *len)
{
    char *u;
    size_t l = 0, sl, max = *len;
    long c, c2;
    int b, be = (**s == '\xFE') ? 1 : (**s == '\xFF') ? 0 : -1;

    if (be == -1) return NULL; // not UTF-16

    u = malloc(max);
    for (sl = 2; sl < *len - 1; sl += 2) {
        c = (be) ? ((long)(*s)[sl] << 8) | (*s)[sl + 1] : // big-endian
                   ((long)(*s)[sl + 1] << 8) | (*s)[sl];  // little-endian
        if (c >= 0xD800 && c <= 0xDFFF && (sl += 2) < *len - 1) { // high-half
            c2 = (be) ? ((long)(*s)[sl] << 8) | (*s)[sl + 1] : // big-endian
                        ((long)(*s)[sl + 1] << 8) | (*s)[sl];  // little-endian
            c = (((c & 0x3FF) << 10) | (c2 & 0x3FF)) + 0x10000;
        }

        while (l + 6 > max) u = realloc(u, max += EZXML_BUFSIZE);
        if (c < 0x80) u[l++] = c; // US-ASCII subset
        else { // multi-byte UTF-8 sequence
            for (b = 0, c2 = c; c2; c2 /= 2) b++; // bits in c
            b = (b - 2) / 5; // bytes in payload;
            u[l++] = (0xFF << (7 - b)) | (c >> (6 * b)); // head
            while (b) u[l++] = 0x80 | ((c >> (6 * --b)) & 0x3F); // payload
        }
    }

    return *s = realloc(u, *len = l);
}

// parse the given xml string and return an ezxml structure
ezxml_t ezxml_parse_str(char *s, size_t len)
{
    ezxml_root_t root = (ezxml_root_t)ezxml_new(NULL);
    char *d, **attr, q, e;
    int l;

    root->m = s;
    if (! len) return ezxml_seterr(root, "root tag missing");
    root->u = ezxml_to_utf8(&s, &len);

    e = s[len - 1];
    s[len - 1] = '\0';

    while (*s && *s != '<') s++; // find first tag
    if (! *s) return ezxml_seterr(root, "root tag missing");

    for (;;) {
        attr = (char **)EZXML_NOATTR;
        d = ++s;
        
        if (isalpha(*s) || *s == '_' || *s == ':' || *s < '\0') { // new tag
            if (! root->cur) return ezxml_seterr(root, "unmatched closing tag");

            s += strcspn(s, EZXML_WS "/>");
            while (isspace(*s)) *(s++) = '\0';
  
            l = 0;
            while (*s && *s != '/' && *s != '>') { // new tag attribute
                attr = (! l) ? malloc(3 * sizeof(char *)) :
                               realloc(attr, (l + 3) * sizeof(char *));
                attr[l] = s;

                s += strcspn(s, EZXML_WS "=/>");
                if (*s == '=' || isspace(*s)) { 
                    *(s++) = '\0';
                    q = *(s += strspn(s, EZXML_WS "="));
                    if (q == '"' || q == '\'') { // attribute value
                        attr[l + 1] = ++s;
                        while (*s && *s != q) s++;
                        if (*s) *(s++) = '\0';
                        else {
                            free(attr);
                            return ezxml_seterr(root, (q == '"') ? 
                                                "missing \"" : "missing '");
                        }
                        ezxml_decode(attr[l + 1], 1);
                    }
                    else attr[l + 1] = "";
                }
                else attr[l + 1] = "";

                attr[(l += 2)] = NULL;
                while (isspace(*s)) s++;
            }

            if (*s == '/') { // self closing tag
                *(s++) = '\0';
                if ((*s && *s != '>') || (! *s && e != '>')) {
                    if (l) free(attr);
                    return ezxml_seterr(root, "missing >");
                }
                ezxml_open_tag(root, d, attr);
                ezxml_close_tag(root);
            }
            else if (*s == '>' || (! *s && e == '>')) { // open tag
                q = *s;
                *s = '\0';
                ezxml_open_tag(root, d, attr);
                *s = q;
            }
            else {
                if (l) free(attr);
                return ezxml_seterr(root, "missing >"); 
            }
        }
        else if (*s == '/') { // close tag
            if (! root->cur) return ezxml_seterr(root, "unmatched closing tag");
            ezxml_close_tag(root);
            while (*s && *s != '>') s++;
            if (! *s && e != '>') return ezxml_seterr(root, "missing >");
        }
        else if (! strncmp(s, "!--", 3)) { // comment
            do { s = strstr(s, "--"); } while (s && *(s += 2) && *s != '>');
            if (! s || (! *s && e != '>'))
                return ezxml_seterr(root, "unclosed <!--");
        }
        else if (! strncmp(s, "![CDATA[", 8)) { // cdata
            if ((s = strstr(s, "]]>")))
                ezxml_char_content(root, d + 8, (s += 2) - d - 10, 0);
            else return ezxml_seterr(root, "unclosed <![CDATA[");
        }
        else if (! strncmp(s, "!DOCTYPE", 8)) { // skip <!DOCTYPE declarations
            for (l = 0; *s && ((! l && *s != '>') || (l && (*s != ']' || 
                 *(s + strspn(s + 1, EZXML_WS) + 1) != '>')));
                 l = (*s == '[') ? 1 : l) s += strcspn(s + 1, "[]>") + 1;
            if (! *s && e != '>') 
                return ezxml_seterr(root, "unclosed <!DOCTYPE");
        }
        else if (*s == '?') { // <?...?> processing instructions
            do { s = strchr(s, '?'); } while (s && *(++s) && *s != '>');
            if (! s || (! *s && e != '>')) 
                return ezxml_seterr(root, "unclosed <?");
            else ezxml_proc_inst(root, d + 1, s - d - 2);
        }
        else return ezxml_seterr(root, "unexpected <");
        
        if (! s || ! *s) break;
        *s = '\0';
        d = ++s;
        if (*s && *s != '<') { // tag character content
            while (*s && *s != '<') s++;
            if (*s) ezxml_char_content(root, d, s - d, 1);
            else break;
        }
        else if (! *s) break;
    }
    
    return (root->cur) ? ezxml_seterr(root, (root->cur->name) ? "unclosed tag" :
                                      "root tag missing") : (ezxml_t)root;
}

// Wrapper for ezxml_parse_str() that accepts a file stream. Reads the entire
// stream into memory and then parses it. For xml files, use ezxml_parse_file()
// or ezxml_parse_fd()
ezxml_t ezxml_parse_fp(FILE *fp)
{
    ezxml_root_t ret;
    size_t l, len = 0;
    void *s;

    if (! (s = malloc(EZXML_BUFSIZE))) return NULL;
    do {
        len += (l = fread((s + len), 1, EZXML_BUFSIZE, fp));
        if (l == EZXML_BUFSIZE) s = realloc(s, len + EZXML_BUFSIZE);
    } while (s && l == EZXML_BUFSIZE);

    if (! s) return NULL;

    ret = (ezxml_root_t)ezxml_parse_str(s, len);
    ret->len = -1; // so we know to free s in ezxml_free()
    return (ezxml_t)ret;
}

// A wrapper for ezxml_parse_str() that accepts a file descriptor. First
// attempts to mem map the file. Failing that, reads the file into memory.
// Returns NULL on failure.
ezxml_t ezxml_parse_fd(int fd)
{
    ezxml_root_t ret;
    struct stat stat;
    size_t len, ps = (size_t)sysconf(_SC_PAGESIZE);
    void *m;

    if (fd < 0) return NULL;

    fstat(fd, &stat);
    len = (stat.st_size + ps - 1) & ~(ps - 1); // round up to next page boundry
 
#ifndef EZXML_NOMMAP
    if ((m = mmap(NULL, len, PROT_READ | PROT_WRITE, MAP_PRIVATE, fd, 0)) !=
        MAP_FAILED) {
        madvise(m, len, MADV_SEQUENTIAL); // optimize for sequential access
        ret = (ezxml_root_t)ezxml_parse_str(m, stat.st_size);
        madvise(m, ret->len = len, MADV_NORMAL); // put it back to normal
    }
    else { // mmap failed, read file into memory
#endif // EZXML_NOMMAP
        len = read(fd, m = malloc(stat.st_size), stat.st_size);
        ret = (ezxml_root_t)ezxml_parse_str(m, len);
        ret->len = -1;
#ifndef EZXML_NOMMAP
    }
#endif // EZXML_NOMMAP

    return (ezxml_t)ret;
}

// a wrapper for ezxml_parse_fd that accepts a file name
ezxml_t ezxml_parse_file(const char *file)
{
    int fd = open(file, O_RDONLY, 0);
    ezxml_t ret = ezxml_parse_fd(fd);
    
    if (fd >= 0) close(fd);
    return ret;
}

// Encodes ampersand sequences appending the results to dst, reallocating dst
// if it's length excedes max. Returns *dst.
char *ezxml_ampencode(const char *s, size_t len, char **dst, size_t *dlen,
                      size_t *max)
{
    const char *e;
    
    for (e = s + len; s != e; s++) {
        while (*dlen + 10 > *max) *dst = realloc(*dst, *max += EZXML_BUFSIZE);

        switch (*s) {
        case '\0': return *dst;
        case '&': *dlen += sprintf(*dst + *dlen, "&amp;"); break;
        case '<': *dlen += sprintf(*dst + *dlen, "&lt;"); break;
        case '>': *dlen += sprintf(*dst + *dlen, "&gt;"); break;
        case '"': *dlen += sprintf(*dst + *dlen, "&quot;"); break;
        default:
            if (*s >= ' ' || *s < '\0' || *s == '\n' || *s == '\t')
                (*dst)[(*dlen)++] = *s;
            else *dlen += sprintf(*dst + *dlen, "&#%02d;", *s);
        }
    }
    
    return *dst;
}

// Recursively converts each tag to xml appending it to s. Reallocates s if it's
// length excedes max. start is the location of the previous tag in the parent
// tag's character content. Returns *s.
char *ezxml_toxml_r(ezxml_t xml, char **s, size_t *len, size_t *max,
                    size_t start)
{
    int i;
    char *txt = (xml->parent) ? xml->parent->txt : "";

    // parent character content up to this tag
    ezxml_ampencode(txt + start, xml->off - start, s, len, max);

    while (*len + strlen(xml->name) + 4 > *max) // reallocate s
        *s = realloc(*s, *max += EZXML_BUFSIZE);

    *len += sprintf(*s + *len, "<%s", xml->name); // open tag
    for (i = 0; xml->attr[i]; i += 2) { // tag attributes
        while (*len + strlen(xml->attr[i]) + 7 > *max) // reallocate s
            *s = realloc(*s, *max += EZXML_BUFSIZE);

        *len += sprintf(*s + *len, " %s=\"", xml->attr[i]);
        ezxml_ampencode(xml->attr[i + 1], -1, s, len, max);
        *len += sprintf(*s + *len, "\"");
    }

    if (xml->child || *(xml->txt)) { // tag content
        *len += sprintf(*s + *len, ">");
        if (xml->child) ezxml_toxml_r(xml->child, s, len, max, 0);
        else ezxml_ampencode(xml->txt, -1, s, len, max); // char content

        while (*len + strlen(xml->name) + 4 > *max) // reallocate s
            *s = realloc(*s, *max += EZXML_BUFSIZE);

        *len += sprintf(*s + *len, "</%s>", xml->name); // close tag
    }
    else *len += sprintf(*s + *len, "/>"); // self closing tag

    if (xml->ordered) return ezxml_toxml_r(xml->ordered, s, len, max, xml->off);
    return ezxml_ampencode(txt + xml->off, -1, s, len, max);
}

// converts an ezxml structure back to xml, returning it as a string that must
// be freed
char *ezxml_toxml(ezxml_t xml)
{
    ezxml_t p = xml->parent;
    size_t len = 0, max = EZXML_BUFSIZE;
    char *s = strcpy(malloc(max), "");

    if (! xml || ! xml->name) return realloc(s, len + 1);

    xml->parent = NULL;
    ezxml_toxml_r(xml, &s, &len, &max, 0);
    xml->parent = p;
    return realloc(s, len + 1);
}

// free the memory allocated for the ezxml structure
void ezxml_free(ezxml_t xml)
{
    ezxml_root_t root = (ezxml_root_t)xml;
    int i;

    if (! xml) return;

    ezxml_free(xml->child);
    ezxml_free(xml->ordered);

    if (! xml->parent) {
        if (root->pi) {
            for (i = 0; root->pi[i]; i++) free(root->pi[i]);
            free(root->pi);
        }

        if (root->len == -1) free(root->m);
#ifndef EZXML_NOMMAP
        else if (root->len) munmap(root->m, root->len);
#endif // EZXML_NOMMAP

        if (root->u) free(root->u);
    }

    if (xml->attr[0]) free(xml->attr);
    if ((xml->flags & EZXML_TXTM)) free(xml->txt);
    free(xml);
}

// return parser error message or empty string if none
const char *ezxml_error(ezxml_t xml)
{
    while (xml->parent) xml = xml->parent;
    return ((ezxml_root_t)xml)->err;
}

// returns a new empty ezxml structure with the given root tag name
ezxml_t ezxml_new(const char *name)
{
    ezxml_root_t root = (ezxml_root_t)memset(malloc(sizeof(struct ezxml_root)), 
                                             '\0', sizeof(struct ezxml_root));

    root->xml.name = name;
    root->xml.attr = EZXML_NOATTR;
    root->cur = (ezxml_t)root;
    root->err = root->xml.txt = "";  
    return (ezxml_t)root;
}

// adds a child tag. offset is the location of the child tag relative to the
// start of the parrent tag's character content. returns the child tag
ezxml_t ezxml_add_child(ezxml_t xml, const char *name, size_t offset)
{
    ezxml_t sub, child = (ezxml_t)malloc(sizeof(struct ezxml));

    if (xml->child) { // already have sub tags
        sub = xml->child;
        while (sub->ordered) sub = sub->ordered;
        sub->ordered = child;

        sub = xml->child;
        while (sub->sibling && strcmp(sub->name, name)) sub = sub->sibling;
        if (! strcmp(sub->name, name)) { // already have this tag type
            while (sub->next) sub = sub->next;
            sub->next = child;
        }
        else sub->sibling = child;
    }
    else xml->child = child; // first sub tag
    
    // initialize new tag
    child->name = name;
    child->attr = EZXML_NOATTR;
    child->off = offset;
    child->parent = xml;
    child->next = child->child = child->sibling = child->ordered = NULL;
    child->txt = "";
    child->flags = 0;
    return child;
}

// sets the character content for the given tag
void ezxml_set_txt(ezxml_t xml, const char *txt)
{
    if (xml->flags & EZXML_TXTM) { // existing txt was malloced
        free(xml->txt);
        xml->flags &= ~EZXML_TXTM;
    }
    xml->txt = (char *)txt;
}

// Sets the given tag attribute or adds a new attribute if not found. A value of
// NULL will remove the specified attribute.
void ezxml_set_attr(ezxml_t xml, const char *name, const char *value)
{
    int l = 0, c = 0;

    while (xml->attr[l] && strcmp(xml->attr[l], name)) l += 2;
    if (! xml->attr[l]) { // not found, add as new attribute
        if (! value) return; // nothing to do
        if (xml->attr == EZXML_NOATTR) xml->attr = malloc(3 * sizeof(char *));
        else xml->attr = realloc(xml->attr, (l + 3) * sizeof(char *));
        xml->attr[l] = name;
        xml->attr[l + 2] = NULL;
    }

    if (value) xml->attr[l + 1] = value; // set attribute
    else { // remove attribute
        while (xml->attr[l + 2 + c]) c += 2; // find end of attribute list
        memmove(xml->attr + l, xml->attr + l + 2, (c + 1) * sizeof(char *));
        xml->attr = realloc(xml->attr, (l + c + 1) * sizeof(char *));
    }
}

// removes a tag along with all its subtags
void ezxml_remove(ezxml_t xml)
{
    ezxml_t cur;

    if (! xml) return; // nothing to do

    if (xml->next) xml->next->sibling = xml->sibling; // patch sibling list

    if (xml->parent) { // not root tag
        cur = xml->parent->child; // find head of subtag list
        if (cur == xml) xml->parent->child = xml->ordered; // first subtag
        else { // not first subtag
            while (cur->ordered != xml) cur = cur->ordered;
            cur->ordered = cur->ordered->ordered; // patch ordered list

            cur = xml->parent->child; // go back to head of subtag list
            if (strcmp(cur->name, xml->name)) { // not in first sibling list
                while(strcmp(cur->sibling->name, xml->name)) cur = cur->sibling;
                if (cur->sibling == xml) { // first of a sibling list
                    if (xml->next) cur->sibling = xml->next;
                    else cur->sibling = cur->sibling->sibling;
                }
                else cur = cur->sibling; // not first of a sibling list
            }

            while (cur->next && cur->next != xml) cur = cur->next;
            if (cur->next) cur->next = cur->next->next; // patch next list
        }        
    }

    xml->ordered = NULL;
    ezxml_free(xml);
}

#ifdef EZXML_TEST // test harness

int main(int argc, char **argv)
{
    ezxml_t xml;
    char *s;

    if (argc != 2) return fprintf(stderr, "usage: %s xmlfile\n", argv[0]);

    xml = ezxml_parse_file(argv[1]);
    printf("%s", (s = ezxml_toxml(xml)));
    fprintf(stderr, "%s", ezxml_error(xml));
    free(s);
    ezxml_free(xml);
    return 0;
}

#endif // EZXML_TEST
