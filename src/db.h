
#ifndef __DB_H__
#define __DB_H__

#include <time.h>
#include <stddef.h>
#include <stdint.h>

#include <sqlite3.h>


enum index_type {
  I_NONE,
  I_FIRST,
  I_LAST,
  I_SUB
};

enum sort_type {
  S_NONE = 0,
  S_NAME,
  S_ALBUM,
  S_ARTIST,
  S_PLAYLIST,
  S_YEAR,
};

#define Q_F_BROWSE (1 << 15)

enum query_type {
  Q_ITEMS            = (1 << 0),
  Q_PL               = (1 << 1),
  Q_PLITEMS          = (1 << 2),
  Q_BROWSE_ARTISTS   = Q_F_BROWSE | (1 << 3),
  Q_BROWSE_ALBUMS    = Q_F_BROWSE | (1 << 4),
  Q_BROWSE_GENRES    = Q_F_BROWSE | (1 << 5),
  Q_BROWSE_COMPOSERS = Q_F_BROWSE | (1 << 6),
  Q_GROUP_ALBUMS     = (1 << 7),
  Q_GROUP_ARTISTS    = (1 << 8),
  Q_GROUP_ITEMS      = (1 << 9),
  Q_GROUP_DIRS       = Q_F_BROWSE | (1 << 10),
  Q_BROWSE_YEARS     = Q_F_BROWSE | (1 << 11),
  Q_COUNT_ITEMS      = (1 << 12),
};

#define ARTWORK_UNKNOWN   0
#define ARTWORK_NONE      1
#define ARTWORK_EMBEDDED  2
#define ARTWORK_OWN       3
#define ARTWORK_DIR       4
#define ARTWORK_PARENTDIR 5
#define ARTWORK_SPOTIFY   6
#define ARTWORK_HTTP      7

enum filelistitem_type {
  F_PLAYLIST = 1,
  F_DIR  = 2,
  F_FILE = 3,
};

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
  sqlite3_stmt *stmt;
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
  DATA_KIND_URL = 1,     /* url/stream */
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
  uint64_t sample_count;
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
};

#define mfi_offsetof(field) offsetof(struct media_file_info, field)

/* PL_SPECIAL value must be in sync with type value in Q_PL* in db.c */
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
};

#define dbmfi_offsetof(field) offsetof(struct db_media_file_info, field)

struct filelist_info {
  char *virtual_path;
  uint32_t time_modified;
  enum filelistitem_type type;
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
  sqlite3_stmt *stmt;
};

struct count_info {
  uint32_t count;
  uint32_t length;
};

char *
db_escape_string(const char *str);

void
free_pi(struct pairing_info *pi, int content_only);

void
free_fi(struct filelist_info *fi, int content_only);

void
free_mfi(struct media_file_info *mfi, int content_only);

void
unicode_fixup_mfi(struct media_file_info *mfi);

void
free_pli(struct playlist_info *pli, int content_only);

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

/* Queries */
int
db_query_start(struct query_params *qp);

void
db_query_end(struct query_params *qp);

int
db_query_fetch_file(struct query_params *qp, struct db_media_file_info *dbmfi);

int
db_query_fetch_pl(struct query_params *qp, struct db_playlist_info *dbpli);

int
db_query_fetch_group(struct query_params *qp, struct db_group_info *dbgri);

int
db_query_fetch_count(struct query_params *qp, struct count_info *ci);

int
db_query_fetch_string(struct query_params *qp, char **string);

int
db_query_fetch_string_sort(struct query_params *qp, char **string, char **sortstring);

/* Files */
int
db_files_get_count(void);

int
db_files_get_count_bymatch(char *path);

void
db_files_update_songartistid(void);

void
db_files_update_songalbumid(void);

void
db_file_inc_playcount(int id);

void
db_file_ping(int id);

void
db_file_ping_bymatch(char *path, int isdir);

char *
db_file_path_byid(int id);

int
db_file_id_bypath(char *path);

int
db_file_id_bymatch(char *path);

int
db_file_id_byfilebase(char *filename, char *base);

int
db_file_id_byfile(char *filename);

int
db_file_id_byurl(char *url);

void
db_file_stamp_bypath(char *path, time_t *stamp, int *id);

struct media_file_info *
db_file_fetch_byid(int id);

struct media_file_info *
db_file_fetch_byvirtualpath(char *path);

int
db_file_add(struct media_file_info *mfi);

int
db_file_update(struct media_file_info *mfi);

void
db_file_update_icy(int id, char *artist, char *album);

void
db_file_delete_bypath(char *path);

void
db_file_disable_bypath(char *path, char *strip, uint32_t cookie);

void
db_file_disable_bymatch(char *path, char *strip, uint32_t cookie);

int
db_file_enable_bycookie(uint32_t cookie, char *path);

/* Playlists */
int
db_pl_get_count(void);

void
db_pl_ping(int id);

void
db_pl_ping_bymatch(char *path, int isdir);

struct playlist_info *
db_pl_fetch_bypath(char *path);

struct playlist_info *
db_pl_fetch_byvirtualpath(char *virtual_path);

struct playlist_info *
db_pl_fetch_bytitlepath(char *title, char *path);

int
db_pl_add(struct playlist_info *pli, int *id);

int
db_pl_add_item_bypath(int plid, char *path);

int
db_pl_add_item_byid(int plid, int fileid);

void
db_pl_clear_items(int id);

int
db_pl_update(struct playlist_info *pli);

void
db_pl_delete(int id);

void
db_pl_delete_bypath(char *path);

void
db_pl_disable_bypath(char *path, char *strip, uint32_t cookie);

void
db_pl_disable_bymatch(char *path, char *strip, uint32_t cookie);

int
db_pl_enable_bycookie(uint32_t cookie, char *path);

/* Groups */
int
db_groups_clear(void);

int
db_group_persistentid_byid(int id, int64_t *persistentid);

/* Filelist */
int
db_mpd_start_query_filelist(struct query_params *qp, char *path);

int
db_mpd_query_fetch_filelist(struct query_params *qp, struct filelist_info *fi);

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
#endif

/* Admin */
int
db_admin_add(const char *key, const char *value);

char *
db_admin_get(const char *key);

int
db_admin_update(const char *key, const char *value);

int
db_admin_delete(const char *key);

/* Speakers */
int
db_speaker_save(uint64_t id, int selected, int volume);

int
db_speaker_get(uint64_t id, int *selected, int *volume);

void
db_speaker_clear_all(void);

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
db_watch_mark_bypath(char *path, char *strip, uint32_t cookie);

void
db_watch_mark_bymatch(char *path, char *strip, uint32_t cookie);

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
