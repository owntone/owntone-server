
#ifndef __DB_H__
#define __DB_H__

#include <time.h>
#include <stddef.h>
#include <stdint.h>

#include "outputs.h"

enum index_type {
  I_NONE,
  I_FIRST,
  I_LAST,
  I_SUB
};

// Keep in sync with sort_clause[]
enum sort_type {
  S_NONE = 0,
  S_NAME,
  S_ALBUM,
  S_ARTIST,
  S_PLAYLIST,
  S_YEAR,
  S_GENRE,
  S_COMPOSER,
  S_DISC,
  S_TRACK,
  S_VPATH,
  S_POS,
  S_SHUFFLE_POS,
};

#define Q_F_BROWSE (1 << 15)

enum query_type {
  Q_ITEMS            = 1,
  Q_PL               = 2,
  Q_FIND_PL          = 3,
  Q_PLITEMS          = 4,
  Q_GROUP_ALBUMS     = 5,
  Q_GROUP_ARTISTS    = 6,
  Q_GROUP_ITEMS      = 7,
  Q_GROUP_DIRS       = 8,
  Q_COUNT_ITEMS      = 9,

  // Keep in sync with browse_clause[]
  Q_BROWSE_ARTISTS   = Q_F_BROWSE | 1,
  Q_BROWSE_ALBUMS    = Q_F_BROWSE | 2,
  Q_BROWSE_GENRES    = Q_F_BROWSE | 3,
  Q_BROWSE_COMPOSERS = Q_F_BROWSE | 4,
  Q_BROWSE_YEARS     = Q_F_BROWSE | 5,
  Q_BROWSE_DISCS     = Q_F_BROWSE | 6,
  Q_BROWSE_TRACKS    = Q_F_BROWSE | 7,
  Q_BROWSE_VPATH     = Q_F_BROWSE | 8,
  Q_BROWSE_PATH      = Q_F_BROWSE | 9,
};

#define ARTWORK_UNKNOWN   0
#define ARTWORK_NONE      1
#define ARTWORK_EMBEDDED  2
#define ARTWORK_OWN       3
#define ARTWORK_DIR       4
#define ARTWORK_PARENTDIR 5
#define ARTWORK_SPOTIFY   6
#define ARTWORK_HTTP      7

#define DB_ADMIN_SCHEMA_VERSION_MAJOR "schema_version_major"
#define DB_ADMIN_SCHEMA_VERSION_MINOR "schema_version_minor"
#define DB_ADMIN_SCHEMA_VERSION "schema_version"
#define DB_ADMIN_QUEUE_VERSION "queue_version"
#define DB_ADMIN_DB_UPDATE "db_update"
#define DB_ADMIN_START_TIME "start_time"
#define DB_ADMIN_LASTFM_SESSION_KEY "lastfm_sk"
#define DB_ADMIN_SPOTIFY_REFRESH_TOKEN "spotify_refresh_token"

/* Max value for media_file_info->rating (valid range is from 0 to 100) */
#define DB_FILES_RATING_MAX 100

struct query_params {
  /* Query parameters, filled in by caller */
  enum query_type type;
  enum index_type idx_type;
  enum sort_type sort;
  int id;
  int64_t persistentid;
  int offset;
  int limit;

  char *filter;

  /* Query results, filled in by query_start */
  int results;

  /* Private query context, keep out */
  void *stmt;
  char buf1[32];
  char buf2[32];
};

struct pairing_info {
  char *remote_id;
  char *name;
  char *guid;
};

enum media_kind {
  MEDIA_KIND_MUSIC = 1,
  MEDIA_KIND_MOVIE = 2,
  MEDIA_KIND_PODCAST = 4,
  MEDIA_KIND_AUDIOBOOK = 8,
  MEDIA_KIND_MUSICVIDEO = 32,
  MEDIA_KIND_TVSHOW = 64,
};

enum data_kind {
  DATA_KIND_FILE = 0,    /* normal file */
  DATA_KIND_HTTP = 1,    /* network stream (radio) */
  DATA_KIND_SPOTIFY = 2, /* iTunes has no spotify data kind, but we use 2 */
  DATA_KIND_PIPE = 3,    /* iTunes has no pipe data kind, but we use 3 */
};

/* Note that fields marked as integers in the metadata map in filescanner_ffmpeg must be uint32_t here */
struct media_file_info {
  char *path;
  uint32_t index;
  char *fname;
  char *title;
  char *artist;
  char *album;
  char *genre;
  char *comment;
  char *type;            /* daap.songformat */
  char *composer;
  char *orchestra;
  char *conductor;
  char *grouping;
  char *url;             /* daap.songdataurl (asul) */

  uint32_t bitrate;
  uint32_t samplerate;
  uint32_t song_length;
  int64_t file_size;
  uint32_t year;         /* TDRC */

  uint32_t track;        /* TRCK */
  uint32_t total_tracks;

  uint32_t disc;         /* TPOS */
  uint32_t total_discs;

  uint32_t time_added;   /* FIXME: time_t */
  uint32_t time_modified;
  uint32_t time_played;

  uint32_t play_count;
  uint32_t seek;
  uint32_t rating;
  uint32_t db_timestamp;

  uint32_t disabled;
  uint32_t bpm;          /* TBPM */

  uint32_t id;

  char *description;     /* daap.songdescription */
  char *codectype;       /* song.codectype, 4 chars max (32 bits) */

  uint32_t item_kind;    /* song or movie */
  uint32_t data_kind;    /* dmap.datakind (asdk) */
  uint64_t sample_count; //TODO [unused] sample count is never set and therefor always 0
  uint32_t compilation;
  char artwork;

  /* iTunes 5+ */
  uint32_t contentrating;

  /* iTunes 6.0.2 */
  uint32_t has_video;
  uint32_t bits_per_sample;

  uint32_t media_kind;
  uint32_t tv_episode_sort;
  uint32_t tv_season_num;
  char *tv_series_name;
  char *tv_episode_num_str; /* com.apple.itunes.episode-num-str, used as a unique episode identifier */
  char *tv_network_name;

  char *album_artist;

  int64_t songartistid;
  int64_t songalbumid;

  char *title_sort;
  char *artist_sort;
  char *album_sort;
  char *composer_sort;
  char *album_artist_sort;

  char *virtual_path;

  uint32_t directory_id; /* Id of directory */
  uint32_t date_released;
};

#define mfi_offsetof(field) offsetof(struct media_file_info, field)

/* PL_SPECIAL value must be in sync with type value in Q_PL* in db_init.c */
enum pl_type {
  PL_SPECIAL = 0,
  PL_FOLDER = 1,
  PL_SMART = 2,
  PL_PLAIN = 3,
  PL_MAX,
};

struct playlist_info {
  uint32_t id;           /* integer id (miid) */
  char *title;           /* playlist name as displayed in iTunes (minm) */
  enum pl_type type;     /* see PL_ types */
  uint32_t items;        /* number of items (mimc) */
  uint32_t streams;      /* number of internet streams */
  char *query;           /* where clause if type 1 (MSPS) */
  uint32_t db_timestamp; /* time last updated */
  uint32_t disabled;
  char *path;            /* path of underlying playlist */
  uint32_t index;        /* index of playlist for paths with multiple playlists */
  uint32_t special_id;   /* iTunes identifies certain 'special' playlists with special meaning */
  char *virtual_path;    /* virtual path of underlying playlist */
  uint32_t parent_id;    /* Id of parent playlist if the playlist is nested */
  uint32_t directory_id; /* Id of directory */
};

#define pli_offsetof(field) offsetof(struct playlist_info, field)

struct db_playlist_info {
  char *id;
  char *title;
  char *type;
  char *items;
  char *streams;
  char *query;
  char *db_timestamp;
  char *disabled;
  char *path;
  char *index;
  char *special_id;
  char *virtual_path;
  char *parent_id;
  char *directory_id;
};

#define dbpli_offsetof(field) offsetof(struct db_playlist_info, field)

struct group_info {
  uint32_t id;           /* integer id (miid) */
  uint64_t persistentid; /* ulonglong id (mper) */
  char *itemname;        /* album or album_artist (minm) */
  char *itemname_sort;   /* album_sort or album_artist_sort (~mshc) */
  uint32_t itemcount;    /* number of items (mimc) */
  uint32_t groupalbumcount; /* number of albums (agac) */
  char *songalbumartist; /* song album artist (asaa) */
  uint64_t songartistid; /* song artist id (asri) */
  uint32_t song_length;
};

#define gri_offsetof(field) offsetof(struct group_info, field)

struct db_group_info {
  char *id;
  char *persistentid;
  char *itemname;
  char *itemname_sort;
  char *itemcount;
  char *groupalbumcount;
  char *songalbumartist;
  char *songartistid;
  char *song_length;
};

#define dbgri_offsetof(field) offsetof(struct db_group_info, field)

struct db_media_file_info {
  char *id;
  char *path;
  char *fname;
  char *title;
  char *artist;
  char *album;
  char *genre;
  char *comment;
  char *type;
  char *composer;
  char *orchestra;
  char *conductor;
  char *grouping;
  char *url;
  char *bitrate;
  char *samplerate;
  char *song_length;
  char *file_size;
  char *year;
  char *track;
  char *total_tracks;
  char *disc;
  char *total_discs;
  char *bpm;
  char *compilation;
  char *artwork;
  char *rating;
  char *play_count;
  char *seek;
  char *data_kind;
  char *item_kind;
  char *description;
  char *time_added;
  char *time_modified;
  char *time_played;
  char *db_timestamp;
  char *disabled;
  char *sample_count;
  char *codectype;
  char *idx;
  char *has_video;
  char *contentrating;
  char *bits_per_sample;
  char *album_artist;
  char *media_kind;
  char *tv_episode_sort;
  char *tv_season_num;
  char *tv_series_name;
  char *tv_episode_num_str;
  char *tv_network_name;
  char *songartistid;
  char *songalbumid;
  char *title_sort;
  char *artist_sort;
  char *album_sort;
  char *composer_sort;
  char *album_artist_sort;
  char *virtual_path;
  char *directory_id;
  char *date_released;
};

#define dbmfi_offsetof(field) offsetof(struct db_media_file_info, field)

enum strip_type {
  STRIP_NONE,
  STRIP_PATH,
};

struct watch_info {
  int wd;
  char *path;
  uint32_t cookie;
};

#define wi_offsetof(field) offsetof(struct watch_info, field)

struct watch_enum {
  uint32_t cookie;
  char *match;

  /* Private enum context, keep out */
  void *stmt;
};

struct filecount_info {
  uint32_t count;
  uint64_t length;
};

/* Directory ids must be in sync with the ids in Q_DIR* in db_init.c */
enum directory_ids {
  DIR_ROOT = 1,
  DIR_FILE = 2,
  DIR_HTTP = 3,
  DIR_SPOTIFY = 4,
  DIR_MAX
};

struct directory_info {
  uint32_t id;
  char *virtual_path;
  uint32_t db_timestamp;
  uint32_t disabled;
  uint32_t parent_id;
};

struct directory_enum {
  int parent_id;

  /* Private enum context, keep out */
  void *stmt;
};

struct db_queue_item
{
  /* A unique id for this queue item. If the same item appears multiple
     times in the queue each corresponding queue item has its own id. */
  uint32_t id;

  /* Id of the file/item in the files database */
  uint32_t file_id;

  /* Length of the item in ms */
  uint32_t song_length;

  /* Data type of the item */
  enum data_kind data_kind;
  /* Media type of the item */
  enum media_kind media_kind;

  uint32_t seek;

  uint32_t pos;
  uint32_t shuffle_pos;

  char *path;
  char *virtual_path;

  char *title;
  char *artist;
  char *album_artist;
  char *album;
  char *genre;

  int64_t songalbumid;
  uint32_t time_modified;

  char *artist_sort;
  char *album_sort;
  char *album_artist_sort;

  uint32_t year;
  uint32_t track;
  uint32_t disc;

  char *artwork_url;

  uint32_t queue_version;
};

char *
db_escape_string(const char *str); // TODO Remove this, use db_mprintf instead

char *
db_mprintf(const char *fmt, ...);

int
db_snprintf(char *s, int n, const char *fmt, ...);

void
free_pi(struct pairing_info *pi, int content_only);

void
free_mfi(struct media_file_info *mfi, int content_only);

void
free_pli(struct playlist_info *pli, int content_only);

void
free_di(struct directory_info *di, int content_only);

void
free_query_params(struct query_params *qp, int content_only);

void
free_queue_item(struct db_queue_item *queue_item, int content_only);

void
unicode_fixup_mfi(struct media_file_info *mfi);

/* Maintenance and DB hygiene */
void
db_hook_post_scan(void);

void
db_purge_cruft(time_t ref);

void
db_purge_all(void);

/* Transactions */
void
db_transaction_begin(void);

void
db_transaction_end(void);

void
db_transaction_rollback(void);

/* Queries */
int
db_query_start(struct query_params *qp);

void
db_query_end(struct query_params *qp);

int
db_query_fetch_file(struct query_params *qp, struct db_media_file_info *dbmfi);

int
db_query_fetch_pl(struct query_params *qp, struct db_playlist_info *dbpli, int with_itemcount);

int
db_query_fetch_group(struct query_params *qp, struct db_group_info *dbgri);

int
db_query_fetch_count(struct query_params *qp, struct filecount_info *fci);

int
db_query_fetch_string(struct query_params *qp, char **string);

int
db_query_fetch_string_sort(struct query_params *qp, char **string, char **sortstring);

/* Files */
int
db_files_get_count(void);

int
db_files_get_artist_count(void);

int
db_files_get_album_count(void);

void
db_file_inc_playcount(int id);

void
db_file_ping(int id);

int
db_file_ping_bypath(const char *path, time_t mtime_max);

void
db_file_ping_bymatch(const char *path, int isdir);

char *
db_file_path_byid(int id);

int
db_file_id_bypath(const char *path);

int
db_file_id_byfile(const char *filename);

int
db_file_id_byurl(const char *url);

int
db_file_id_by_virtualpath_match(const char *path);

struct media_file_info *
db_file_fetch_byid(int id);

struct media_file_info *
db_file_fetch_byvirtualpath(const char *path);

int
db_file_add(struct media_file_info *mfi);

int
db_file_update(struct media_file_info *mfi);

void
db_file_seek_update(int id, uint32_t seek);

int
db_file_rating_update_byid(uint32_t id, uint32_t rating);

int
db_file_rating_update_byvirtualpath(const char *virtual_path, uint32_t rating);

void
db_file_delete_bypath(const char *path);

void
db_file_disable_bypath(const char *path, enum strip_type strip, uint32_t cookie);

void
db_file_disable_bymatch(const char *path, enum strip_type strip, uint32_t cookie);

int
db_file_enable_bycookie(uint32_t cookie, const char *path, const char *filename);

int
db_file_update_directoryid(const char *path, int dir_id);

int
db_filecount_get(struct filecount_info *fci, struct query_params *qp);

/* Playlists */
int
db_pl_get_count(void);

void
db_pl_ping(int id);

void
db_pl_ping_bymatch(const char *path, int isdir);

void
db_pl_ping_items_bymatch(const char *path, int id);

int
db_pl_id_bypath(const char *path);

struct playlist_info *
db_pl_fetch_bypath(const char *path);

struct playlist_info *
db_pl_fetch_byvirtualpath(const char *virtual_path);

struct playlist_info *
db_pl_fetch_bytitlepath(const char *title, const char *path);

int
db_pl_add(struct playlist_info *pli, int *id);

int
db_pl_add_item_bypath(int plid, const char *path);

int
db_pl_add_item_byid(int plid, int fileid);

void
db_pl_clear_items(int id);

int
db_pl_update(struct playlist_info *pli);

void
db_pl_delete(int id);

void
db_pl_delete_bypath(const char *path);

void
db_pl_disable_bypath(const char *path, enum strip_type strip, uint32_t cookie);

void
db_pl_disable_bymatch(const char *path, enum strip_type strip, uint32_t cookie);

int
db_pl_enable_bycookie(uint32_t cookie, const char *path);

/* Groups */
int
db_groups_cleanup();

int
db_group_persistentid_byid(int id, int64_t *persistentid);


/* Directories */
int
db_directory_id_byvirtualpath(char *virtual_path);

int
db_directory_enum_start(struct directory_enum *de);

int
db_directory_enum_fetch(struct directory_enum *de, struct directory_info *di);

void
db_directory_enum_end(struct directory_enum *de);

int
db_directory_addorupdate(char *virtual_path, int disabled, int parent_id);

void
db_directory_ping_bymatch(char *virtual_path);

void
db_directory_disable_bymatch(char *path, enum strip_type strip, uint32_t cookie);

int
db_directory_enable_bycookie(uint32_t cookie, char *path);

int
db_directory_enable_bypath(char *path);

/* Remotes */
int
db_pairing_add(struct pairing_info *pi);

int
db_pairing_fetch_byguid(struct pairing_info *pi);

#ifdef HAVE_SPOTIFY_H
/* Spotify */
void
db_spotify_purge(void);

void
db_spotify_pl_delete(int id);

void
db_spotify_files_delete(void);
#endif

/* Admin */
int
db_admin_set(const char *key, const char *value);

int
db_admin_setint(const char *key, int value);

int
db_admin_setint64(const char *key, int64_t value);

char *
db_admin_get(const char *key);

int
db_admin_getint(const char *key);

int64_t
db_admin_getint64(const char *key);

int
db_admin_delete(const char *key);

/* Speakers/outputs */
int
db_speaker_save(struct output_device *device);

int
db_speaker_get(struct output_device *device, uint64_t id);

void
db_speaker_clear_all(void);

/* Queue */
int
db_queue_update_item(struct db_queue_item *queue_item);

int
db_queue_add_by_queryafteritemid(struct query_params *qp, uint32_t item_id);

int
db_queue_add_by_query(struct query_params *qp, char reshuffle, uint32_t item_id);

int
db_queue_add_by_playlistid(int plid, char reshuffle, uint32_t item_id);

int
db_queue_add_by_fileid(int id, char reshuffle, uint32_t item_id);

int
db_queue_add_item(struct db_queue_item *queue_item, char reshuffle, uint32_t item_id);

int
db_queue_enum_start(struct query_params *qp);

void
db_queue_enum_end(struct query_params *qp);

int
db_queue_enum_fetch(struct query_params *qp, struct db_queue_item *queue_item);

struct db_queue_item *
db_queue_fetch_byitemid(uint32_t item_id);

struct db_queue_item *
db_queue_fetch_byfileid(uint32_t file_id);

struct db_queue_item *
db_queue_fetch_bypos(uint32_t pos, char shuffle);

struct db_queue_item *
db_queue_fetch_byposrelativetoitem(int pos, uint32_t item_id, char shuffle);

struct db_queue_item *
db_queue_fetch_next(uint32_t item_id, char shuffle);

struct db_queue_item *
db_queue_fetch_prev(uint32_t item_id, char shuffle);

int
db_queue_cleanup();

int
db_queue_clear(uint32_t keep_item_id);

int
db_queue_delete_byitemid(uint32_t item_id);

int
db_queue_delete_bypos(uint32_t pos, int count);

int
db_queue_delete_byposrelativetoitem(uint32_t pos, uint32_t item_id, char shuffle);

int
db_queue_move_byitemid(uint32_t item_id, int pos_to, char shuffle);

int
db_queue_move_bypos(int pos_from, int pos_to);

int
db_queue_move_byposrelativetoitem(uint32_t from_pos, uint32_t to_offset, uint32_t item_id, char shuffle);

int
db_queue_reshuffle(uint32_t item_id);

int
db_queue_get_count();

int
db_queue_get_pos(uint32_t item_id, char shuffle);

int
db_queue_get_pos_byfileid(uint32_t file_id, char shuffle);

/* Inotify */
int
db_watch_clear(void);

int
db_watch_add(struct watch_info *wi);

int
db_watch_delete_bywd(uint32_t wd);

int
db_watch_delete_bypath(char *path);

int
db_watch_delete_bymatch(char *path);

int
db_watch_delete_bycookie(uint32_t cookie);

int
db_watch_get_bywd(struct watch_info *wi);

int
db_watch_get_bypath(struct watch_info *wi);

void
db_watch_mark_bypath(char *path, enum strip_type strip, uint32_t cookie);

void
db_watch_mark_bymatch(char *path, enum strip_type strip, uint32_t cookie);

void
db_watch_move_bycookie(uint32_t cookie, char *path);

int
db_watch_cookie_known(uint32_t cookie);

int
db_watch_enum_start(struct watch_enum *we);

void
db_watch_enum_end(struct watch_enum *we);

int
db_watch_enum_fetchwd(struct watch_enum *we, uint32_t *wd);


int
db_perthread_init(void);

void
db_perthread_deinit(void);

int
db_init(void);

void
db_deinit(void);

#endif /* !__DB_H__ */
