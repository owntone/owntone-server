/*
 * $Id: $
 */

#ifndef _MTD_PLUGINS_H_
#define _MTD_PLUGINS_H_

#define PLUGIN_OUTPUT    0
#define PLUGIN_SCANNER   1
#define PLUGIN_DATABASE  2
#define PLUGIN_OTHER     3

#define PLUGIN_VERSION   1

struct tag_wsconninfo;
typedef struct tag_wsconninfo WS_CONNINFO;

typedef struct tag_plugin_output_fn {
    void (*handler)(WS_CONNINFO *pwsc);
} PLUGIN_OUTPUT_FN;

typedef struct tag_plugin_info {
    int version;
    int type;
    char *server;
    char *url;     /* regex of namespace to handle if OUTPUT type */
    void *handler_functions;
} PLUGIN_INFO;

/* these are the functions that must be exported by the plugin */
PLUGIN_INFO *plugin_info(void);

/* xml helpers for output plugins */
struct tag_xmlstruct;
typedef struct tag_xmlstruct XMLSTRUCT;
typedef void *PARSETREE;

extern XMLSTRUCT *pi_xml_init(WS_CONNINFO *pwsc, int emit_header);
extern void pi_xml_push(XMLSTRUCT *pxml, char *term);
extern void pi_xml_pop(XMLSTRUCT *pxml);
extern void pi_xml_output(XMLSTRUCT *pxml, char *section, char *fmt, ...);
extern void pi_xml_deinit(XMLSTRUCT *pxml);

/* webserver helpers for output plugins */
extern char *pi_ws_uri(WS_CONNINFO *pwsc);
extern void pi_ws_close(WS_CONNINFO *pwsc);
extern int pi_ws_returnerror(WS_CONNINFO *pwsc, int error, char *description);
extern char *pi_ws_getvar(WS_CONNINFO *pwsc, char *var);

/* misc helpers */
extern char *pi_server_ver(void);
extern int pi_server_name(char *, int *);

/* logging */
#define E_FATAL 0
#define E_LOG   1
#define E_INF   5
#define E_DBG   9

void pi_log(int level, char *fmt, ...);



/* db stuff */
typedef enum {
    queryTypeItems,
    queryTypePlaylists,
    queryTypePlaylistItems,
    queryTypeBrowseArtists,
    queryTypeBrowseAlbums,
    queryTypeBrowseGenres,
    queryTypeBrowseComposers
} QueryType_t;

typedef enum {
    indexTypeNone,
    indexTypeFirst,
    indexTypeLast,
    indexTypeSub
} IndexType_t;

typedef enum {
    // generic meta data
    metaItemId,
    metaItemName,
    metaItemKind,
    metaPersistentId,
    metaContainerItemId,
    metaParentContainerId,

    firstTypeSpecificMetaId,

    // song meta data
    metaSongAlbum = firstTypeSpecificMetaId,
    metaSongArtist,
    metaSongBPM,
    metaSongBitRate,
    metaSongComment,
    metaSongCompilation,
    metaSongComposer,
    metaSongDataKind,
    metaSongDataURL,
    metaSongDateAdded,
    metaSongDateModified,
    metaSongDescription,
    metaSongDisabled,
    metaSongDiscCount,
    metaSongDiscNumber,
    metaSongEqPreset,
    metaSongFormat,
    metaSongGenre,
    metaSongGrouping,
    metaSongRelativeVolume,
    metaSongSampleRate,
    metaSongSize,
    metaSongStartTime,
    metaSongStopTime,
    metaSongTime,
    metaSongTrackCount,
    metaSongTrackNumber,
    metaSongUserRating,
    metaSongYear,

    /* iTunes 4.5 + */
    metaSongCodecType,
    metaSongCodecSubType,
    metaItunesNormVolume,
    metaItmsSongId,
    metaItmsArtistId,
    metaItmsPlaylistId,
    metaItmsComposerId,
    metaItmsGenreId,
    metaItmsStorefrontId,
    metaItunesSmartPlaylist,

    /* iTunes 5.0 + */
    metaSongContentRating,
    metaHasChildContainers,

    /* iTunes 6.0.2+ */
    metaItunesHasVideo,

    /* mt-daapd specific */
    metaMPlaylistSpec,
    metaMPlaylistType
} MetaFieldName_t;

typedef unsigned long long MetaField_t;

typedef struct tag_dbqueryinfo {
    QueryType_t query_type;
    IndexType_t index_type;
    MetaField_t meta;
    int zero_length; /* emit zero-length strings? */
    int index_low;
    int index_high;
    int playlist_id;
    int db_id;
    int session_id;
    int want_count;
    int specifiedtotalcount;
    int uri_count;
    char *uri_sections[10];
    PARSETREE pt;
    void *output_info;
} DBQUERYINFO;


/* db helpers */
extern int pi_db_count(void);
extern int pi_db_enum_start(char **pe, DBQUERYINFO *pinfo);
extern int pi_db_enum_fetch_row(char **pe, char ***row, DBQUERYINFO *pinfo);
extern int pi_db_enum_end(char **pe);
extern void pi_stream(WS_CONNINFO *pwsc, DBQUERYINFO *pqi, char *id);

/* smart parser helpers */
extern PARSETREE pi_sp_init(void);
extern int pi_sp_parse(PARSETREE tree, char *term);
extern int pi_sp_dispose(PARSETREE tree);
extern char *pi_sp_get_error(PARSETREE tree);


#endif /* _MTD_PLUGINS_H_ */
