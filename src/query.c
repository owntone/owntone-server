#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <stdio.h>
#include <ctype.h>

#include "db-generic.h"
#include "err.h"
#include "query.h"

static query_node_t* query_build(const char* query);
static void query_free(query_node_t* query);
static int query_build_clause(query_node_t *query, char **current, int *size); 

static const query_field_t *find_field(const char* name, 
				       const query_field_t* fields);
// static int arith_query(query_node_t* query, void* target);
// static int string_query(query_node_t* query, void* target);

static query_node_t *match_specifier(const char* query,
				     const char** cursor,
				     const query_field_t* fields);

static query_node_t *group_match(const char* query,
				 const char** cursor,
				 const query_field_t* fields);

static query_node_t *single_match(const char* query,
				  const char** cursor,
				  const query_field_t* fields);

static int get_field_name(const char** pcursor,
			  const char* query, 
			  char* name, 
			  int len);

static query_node_t *match_number(const query_field_t* field, 
				  char not, char opcode,
				  const char** pcursor, 
				  const char* query);

static query_node_t *match_string(const query_field_t* field, 
				  char not, char opcode,
				  const char** pcursor, 
				  const char* query);

char *query_unescape(const char* query);


static query_field_t	song_fields[] = {
    { qft_string,	"dmap.itemname",	"title" },
    { qft_i32,		"dmap.itemid",		"id" },
    { qft_string,	"daap.songalbum",	"album" },
    { qft_string,	"daap.songartist",	"artist" },
    { qft_i32,		"daap.songbitrate",     "bitrate" },
    { qft_string,	"daap.songcomment",	"comment" },
    { qft_i32,  	"daap.songcompilation",	"compilation" },
    { qft_string,	"daap.songcomposer",	"composer" },
    { qft_i32,  	"daap.songdatakind",    "data_kind" },
    { qft_string,	"daap.songdataurl",	"url" },
    { qft_i32,		"daap.songdateadded",	"time_added" },
    { qft_i32,		"daap.songdatemodified","time_modified" },
    { qft_string,	"daap.songdescription",	"description" },
    { qft_i32,		"daap.songdisccount",	"total_discs" },
    { qft_i32,		"daap.songdiscnumber",	"disc" },
    { qft_string,	"daap.songformat",	"type" },
    { qft_string,	"daap.songgenre",	"genre" },
    { qft_i32,		"daap.songsamplerate",	"samplerate" },
    { qft_i32,		"daap.songsize",	"file_size" },
    //    { qft_i32_const,	"daap.songstarttime",	0 },
    { qft_i32,		"daap.songstoptime",	"song_length" },
    { qft_i32,		"daap.songtime",	"song_length" },
    { qft_i32,		"daap.songtrackcount",	"total_tracks" },
    { qft_i32,		"daap.songtracknumber",	"track" },
    { qft_i32,		"daap.songyear",	"year" },
    { 0,                NULL,                   NULL }
};

char *query_sql_escape(char *term) {
    int new_size=0;
    char *src, *dst;
    char *pnew;

    src=term;
    while(*src) {
	new_size++;
	if(*src == '\'')
	    new_size++;
	src++;
    }

    src=term;
    pnew=(char*)malloc(new_size+1);
    if(!pnew)
	return pnew;

    dst=pnew;
    while(*src) {
	if(*src == '\'')
	    *dst++='\'';
	*dst++=*src++;
    }

    *dst='\0';
    return pnew;
}

char *query_build_sql(char *query) {
    query_node_t *pquery;
    char sql[2048];
    char *sqlptr=sql;
    int size=sizeof(sql);

    pquery=query_build(query);
    if(pquery) {
	if(!query_build_clause(pquery,&sqlptr,&size)) {
	    query_free(pquery);
	    return strdup(sql);
	}
	query_free(pquery);
    }
    return NULL;
}


query_node_t* query_build(const char* query) {
    query_node_t*	left = 0;
    char*		raw = query_unescape(query);
    const char*		cursor = raw;
    query_node_t*	right = 0;
    query_type_t	join;

    if(0 == (left = match_specifier(query, &cursor, song_fields)))
	goto error;

    while(*cursor)
    {
	query_node_t*	con;

	switch(*cursor)
	{
	case '+':
	case ' ':	join = qot_and;		break;
	case ',':	join = qot_or;		break;
	default:
	    DPRINTF(E_LOG,L_QRY, "Illegal character '%c' (0%o) at index %d: %s\n",
		    *cursor, *cursor, cursor - raw, raw);
	    goto error;
	}

	cursor++;

	if(0 == (right = match_specifier(raw, &cursor, song_fields)))
	    goto error;

	con = (query_node_t*) calloc(1, sizeof(*con));
	con->type = join;
	con->left.node = left;
	con->right.node = right;

	left = con;
    }

    if(query != raw)
	free(raw);

    return left;

 error:
    if(left != 0)
	query_free(left);
    if(raw != query)
	free(raw);

    return NULL;
}

static query_node_t*	match_specifier(const char* query,
					const char** cursor,
					const query_field_t* fields) {
    switch(**cursor) {
    case '\'':		
	return single_match(query, cursor, fields);
    case '(':		
	return group_match(query, cursor, fields);
    }

    DPRINTF(E_LOG,L_QRY,"Illegal character '%c' (0%o) at index %d: %s\n",
	    **cursor, **cursor, *cursor - query, query);
    return NULL;
}

static query_node_t*	group_match(const char* query,
				    const char** pcursor,
				    const query_field_t* fields)
{
    query_node_t*	left = 0;
    query_node_t*	right = 0;
    query_node_t*	join = 0;
    query_type_t	opcode;
    const char*		cursor = *pcursor;

    /* skip the opening ')' */
    ++cursor;

    if(0 == (left = single_match(query, &cursor, fields)))
	return NULL;

    switch(*cursor)
    {
    case '+':
    case ' ':
	opcode = qot_and;
	break;

    case ',':
	opcode = qot_or;
	break;

    default:
	DPRINTF(E_LOG,L_QRY,"Illegal character '%c' (0%o) at index %d: %s\n",
		*cursor, *cursor, cursor - query, query);
	goto error;
    }

    if(0 == (right = single_match(query, &cursor, fields)))
	goto error;

    if(*cursor != ')')
    {
	DPRINTF(E_LOG,L_QRY,"Illegal character '%c' (0%o) at index %d: %s\n",
		*cursor, *cursor, cursor - query, query);
	goto error;
    }
	
    *pcursor = cursor + 1;

    join = (query_node_t*) calloc(1, sizeof(*join));
    join->type = opcode;
    join->left.node = left;
    join->right.node = right;

    return join;

 error:
    if(0 != left)
	query_free(left);
    if(0 != right)
	query_free(right);

    return 0;
}

static query_node_t*	single_match(const char* query,
				     const char** pcursor,
				     const query_field_t* fields)
{
    char			fname[64];
    const query_field_t*	field;
    char			not = 0;
    char			op = 0;
    query_node_t*		node = 0;

    /* skip opening ' */
    (*pcursor)++;

    /* collect the field name */
    if(!get_field_name(pcursor, query, fname, sizeof(fname)))
	return NULL;

    if(**pcursor == '!')
    {
	not = '!';
	++(*pcursor);
    }

    if(strchr(":+-", **pcursor))
    {
	op = **pcursor;
	++(*pcursor);
    }
    else
    {
	DPRINTF(E_LOG,L_QRY,"Illegal Operator: %c (0%o) at index %d: %s\n",
		**pcursor, **pcursor, *pcursor - query, query);
	return NULL;
    }

    if(0 == (field = find_field(fname, fields)))
    {
	DPRINTF(E_LOG,L_QRY,"Unknown field: %s\n", fname);
	return NULL;
    }

    switch(field->type)
    {
    case qft_i32:
    case qft_i64:
	node = match_number(field, not, op, pcursor, query);
	break;

    case qft_string:
	node = match_string(field, not, op, pcursor, query);
	break;

    default:
	DPRINTF(E_LOG,L_QRY,"Invalid field type: %d\n", field->type);
	break;
    }

    if(**pcursor != '\'')
    {
	DPRINTF(E_LOG,L_QRY,"Illegal Character: %c (0%o) index %d: %s\n",
		**pcursor, **pcursor, *pcursor - query, query);
	query_free(node);
	node = 0;
    }
    else
	++(*pcursor);

    return node;
}

static int get_field_name(const char** pcursor,
			  const char* query, 
			  char* name, 
			  int len) {
    const char*	cursor = *pcursor;

    if(!isalpha(*cursor))
	return 0;

    while(isalpha(*cursor) || *cursor == '.') {
	if(--len <= 0) {
	    DPRINTF(E_LOG,L_QRY,"token length exceeded at offset %d: %s\n",
		    cursor - query, query);
	    return 0;
	}

	*name++ = *cursor++;
    }

    *pcursor = cursor;

    *name = 0;

    return 1;
}

static query_node_t*	match_number(const query_field_t* field, 
				     char not, char opcode,
				     const char** pcursor, 
				     const char* query)
{
    query_node_t*	node = (query_node_t*) calloc(1, sizeof(*node));

    switch(opcode)
    {
    case ':':
	node->type = not ? qot_ne : qot_eq;
	break;
    case '+':
    case ' ':
	node->type = not ? qot_le : qot_gt;
	break;
    case '-':
	node->type = not ? qot_ge : qot_lt;
	break;
    }

    node->left.field = field;

    switch(field->type)
    {
    case qft_i32:
	node->right.i32 = strtol(*pcursor, (char**) pcursor, 10);
	break;
    case qft_i64:
	node->right.i64 = strtoll(*pcursor, (char**) pcursor, 10);
	break;
    default:
	DPRINTF(E_LOG,L_QRY,"Bad field type -- invalid query\n");
	break;
    }

    if(**pcursor != '\'')
    {
	DPRINTF(E_LOG,L_QRY,"Illegal char in number '%c' (0%o) at index %d: %s\n",
		**pcursor, **pcursor, *pcursor - query, query);
	free(node);
	return 0;
    }

    return node;
}

static query_node_t*	match_string(const query_field_t* field, 
				     char not, char opcode,
				     const char** pcursor, 
				     const char* query)
{
    char		match[256];
    char*		dst = match;
    int			left = sizeof(match);
    const char*		cursor = *pcursor;
    query_type_t	op = qot_is;
    query_node_t*	node;

    if(opcode != ':') {
	DPRINTF(E_LOG,L_QRY,"Illegal operation on string: %c at index %d: %s\n",
		opcode, cursor - query - 1);
	return NULL;
    }

    if(*cursor == '*') {
	op = qot_ends;
	cursor++;
    }

    while(*cursor && *cursor != '\'') {
	if(--left == 0) {
	    DPRINTF(E_LOG,L_QRY,"string too long at index %d: %s\n",
		    cursor - query, query);
	    return NULL;
	}

	if(*cursor == '\\') {
	    switch(*++cursor) {
	    case '*':
	    case '\'':
	    case '\\':
		*dst++ = *cursor++;
		break;
	    default:
		DPRINTF(E_LOG,L_QRY,"Illegal escape: %c (0%o) at index %d: %s\n",
			*cursor, *cursor, cursor - query, query);
		return NULL;
	    }
	} else {
	    *dst++ = *cursor++;
	}
    }

    if(dst[-1] == '*') {
	op = (op == qot_is) ? qot_begins : qot_contains;
	dst--;
    }

    *dst = 0;

    node = (query_node_t*) calloc(1, sizeof(*node));
    node->type = op;
    node->left.field = field;
    node->right.str = query_sql_escape(match);

    *pcursor = cursor;

    return node;
}


void query_free(query_node_t* query) {
    if(0 != query)
    {
	switch(query->type)
	{
	    // conjunction 
	case qot_and:
	case qot_or:
	    query_free(query->left.node);
	    query_free(query->right.node);
	    break;

	    // negation
	case qot_not:
	    query_free(query->left.node);
	    break;

	    // arithmetic
	case qot_eq:
	case qot_ne:
	case qot_le:
	case qot_lt:
	case qot_ge:
	case qot_gt:
	    break;

	    // string
	case qot_is:
	case qot_begins:
	case qot_ends:
	case qot_contains:
	    free(query->right.str);
	    break;

	    // constants 
	case qot_const:
	    break;
	    
	default:
	    DPRINTF(E_LOG,L_QRY,"Illegal query type: %d\n", query->type);
	    break;
	}

	free(query);
    }
}

static const query_field_t* find_field(const char* name, const query_field_t* fields) {
    while(fields->name && strcasecmp(fields->name, name))
	fields++;

    if(fields->name == 0) {
	DPRINTF(E_LOG,L_QRY,"Illegal query field: %s\n", name);
	return NULL;
    }

    return fields;
}

int query_add_string(char **current, int *size, char *fmt, ...) {
    va_list ap;
    int write_size;

    va_start(ap, fmt);
    write_size=vsnprintf(*current, *size, fmt, ap);
    va_end(ap);

    if(write_size > *size) {
	*size=0;
	return 0;
    }

    *size = *size - write_size;
    return write_size;
}

int query_build_clause(query_node_t *query, char **current, int *size) {
    char* labels[] = {
	"NOP",
	"AND",
	"OR",
	"NOT",
	"=",
	"<>",
	"<=",
	"<",
	">=",
	">",
	"=",
	" (%s LIKE '%s%%') ",
	" (%s LIKE '%%%s') ",
	" (%s LIKE '%%%s%%') ",
	"constant"
    };

    switch(query->type) {
    case qot_and:
    case qot_or:
	if(*size) (*current) += query_add_string(current,size," (");
	if(query_build_clause(query->left.node,current,size)) return 1;
	if(*size) (*current) += query_add_string(current,size," %s ", labels[query->type]);
	if(query_build_clause(query->right.node,current,size)) return 1;
	if(*size) (*current) += query_add_string(current,size,") ");
	break;

    case qot_not:
	if(*size) (*current) += query_add_string(current,size," (NOT ");
	if(query_build_clause(query->left.node,current,size)) return 1;
	if(*size) (*current) += query_add_string(current,size,") ");
	break;
	
    case qot_eq:
    case qot_ne:
    case qot_le:
    case qot_lt:
    case qot_ge:
    case qot_gt:
	if(*size) (*current) += query_add_string(current,size," (%s %s ",
						 query->left.field->fieldname,
						 labels[query->type]);
	if(query->left.field->type == qft_i32) {
	    if(*size) (*current) += query_add_string(current,size," %d) ",query->right.i32);
	} else {
	    if(*size) (*current) += query_add_string(current,size," %ll) ",query->right.i64);
	}
	break;


    case qot_is:
	if(*size)(*current) += query_add_string(current,size," (%s='%s') ", 
						query->left.field->fieldname,
						query->right.str);
	break;
    case qot_begins:
    case qot_ends:
    case qot_contains:
	if(*size)(*current) += query_add_string(current,size,labels[query->type],
						query->left.field->fieldname,
						query->right.str);
	break;
    case qot_const:  /* Not sure what this would be for */
	break; 
    default:
	break;
    }

    return 0;
}

char* query_unescape(const char* src) {

    char*	copy = malloc(strlen(src) + 1);
    char*	dst = copy;

    while(*src)
    {
	if(*src == '%')
	{
	    int		val = 0;

	    if(*++src)
	    {
		if(isdigit(*src))
		    val = val * 16 + *src - '0';
		else
		    val = val * 16 + tolower(*src) - 'a' + 10;
	    }

	    if(*++src)
	    {
		if(isdigit(*src))
		    val = val * 16 + *src - '0';
		else
		    val = val * 16 + tolower(*src) - 'a' + 10;
	    }

	    src++;
	    *dst++ = val;
	}
	else
	    *dst++ = *src++;
    }

    *dst++ = 0;

    return copy;
}
