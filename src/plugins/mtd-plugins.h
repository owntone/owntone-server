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


typedef void* PARSETREE;

struct tag_ws_conninfo;
typedef struct tag_ws_conninfo WS_CONNINFO;

typedef struct tag_plugin_output_fn {
    void(*handler)(WS_CONNINFO *pwsc);
    int(*auth)(WS_CONNINFO *pwsc, char *username, char *pw);
} PLUGIN_OUTPUT_FN;

typedef struct tag_plugin_info {
    int version;
    int type;
    char *server;
    char *url;     /* regex of namespace to handle if OUTPUT type */
    void *handler_functions;
    void *fn; /* input functions*/
} PLUGIN_INFO;

/* xml helpers for output plugins */

/* logging */
#define E_FATAL 0
#define E_LOG   1
#define E_INF   5
#define E_DBG   9

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

typedef struct tag_plugin_input_fn {
    /* webserver helpers */
    char* (*ws_uri)(WS_CONNINFO *);
    void (*ws_close)(WS_CONNINFO *);
    int (*ws_returnerror)(WS_CONNINFO *, int, char *);
    char* (*ws_getvar)(WS_CONNINFO *, char *);
    int (*ws_writefd)(WS_CONNINFO *, char *, ...);
    int (*ws_addresponseheader)(WS_CONNINFO *, char *, char *, ...);
    void (*ws_emitheaders)(WS_CONNINFO *);
    int (*ws_fd)(WS_CONNINFO *);
    char* (*ws_getrequestheader)(WS_CONNINFO *, char *);
    int (*ws_writebinary)(WS_CONNINFO *, char *, int);

    /* misc helpers */
    char* (*server_ver)(void);
    int (*server_name)(char *, int *);
    void (*log)(int, char *, ...);

    int (*db_count)(void);
    int (*db_enum_start)(char **, DBQUERYINFO *);
    int (*db_enum_fetch_row)(char **, char ***, DBQUERYINFO *);
    int (*db_enum_end)(char **);
    void (*stream)(WS_CONNINFO *, DBQUERYINFO *, char *);

    PARSETREE (*sp_init)(void);
    int (*sp_parse)(PARSETREE tree, char *term);
    int (*sp_dispose)(PARSETREE tree);
    char* (*sp_get_error)(PARSETREE tree);
} PLUGIN_INPUT_FN;


/* these are the functions that must be exported by the plugin */
PLUGIN_INFO *plugin_info(void);


#endif /* _MTD_PLUGINS_H_ */
