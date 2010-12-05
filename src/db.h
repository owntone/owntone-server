
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
  Q_GROUPS           = (1 << 7),
  Q_GROUPITEMS       = (1 << 8),
  Q_GROUP_DIRS       = Q_F_BROWSE | (1 << 9),
};

struct query_params {
  /* Query parameters, filled in by caller */
  enum query_type type;
  enum index_type idx_type;
  enum sort_type sort;
  int id;
  int offset;
  int limit;

  char *filter;

  /* Query results, filled in by query_start */
  int results;

  /* Private query context, keep out */
  sqlite3_stmt *stmt;
  char buf[32];
};

struct pairing_info {
  char *remote_id;
  char *name;
  char *guid;
};

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
  char compilation;

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

  int64_t songalbumid;

  char *title_sort;
  char *artist_sort;
  char *album_sort;
  char *composer_sort;
  char *album_artist_sort;
};

#define mfi_offsetof(field) offsetof(struct media_file_info, field)

enum pl_type {
  PL_PLAIN,
  PL_SMART,
  PL_MAX
};

struct playlist_info {
  uint32_t id;           /* integer id (miid) */
  char *title;           /* playlist name as displayed in iTunes (minm) */
  enum pl_type type;     /* see PL_ types */
  uint32_t items;        /* number of items (mimc) */
  char *query;           /* where clause if type 1 (MSPS) */
  uint32_t db_timestamp; /* time last updated */
  uint32_t disabled;
  char *path;            /* path of underlying playlist */
  uint32_t index;        /* index of playlist for paths with multiple playlists */
  uint32_t special_id;   /* iTunes identifies certain 'special' playlists with special meaning */
};

#define pli_offsetof(field) offsetof(struct playlist_info, field)

struct db_playlist_info {
  char *id;
  char *title;
  char *type;
  char *items;
  char *query;
  char *db_timestamp;
  char *disabled;
  char *path;
  char *index;
  char *special_id;
};

#define dbpli_offsetof(field) offsetof(struct db_playlist_info, field)

struct group_info {
  uint32_t id;           /* integer id (miid) */
  uint64_t persistentid; /* ulonglong id (mper) */
  char *itemname;        /* playlist name as displayed in iTunes (minm) */
  uint32_t itemcount;    /* number of items (mimc) */
  char *songalbumartist; /* song album artist (asaa) */
};

#define gri_offsetof(field) offsetof(struct group_info, field)

struct db_group_info {
  char *id;
  char *persistentid;
  char *itemname;
  char *itemcount;
  char *songalbumartist;
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
  char *rating;
  char *play_count;
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
  char *songalbumid;
  char *title_sort;
  char *artist_sort;
  char *album_sort;
  char *composer_sort;
  char *album_artist_sort;
};

#define dbmfi_offsetof(field) offsetof(struct db_media_file_info, field)

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


char *
db_escape_string(const char *str);

void
free_pi(struct pairing_info *pi, int content_only);

void
free_mfi(struct media_file_info *mfi, int content_only);

void
unicode_fixup_mfi(struct media_file_info *mfi);

void
free_pli(struct playlist_info *pli, int content_only);

void
db_purge_cruft(time_t ref);

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
db_query_fetch_string(struct query_params *qp, char **string);

int
db_query_fetch_string_sort(struct query_params *qp, char **string, char **sortstring);

/* Files */
int
db_files_get_count(void);

void
db_files_update_songalbumid(void);

void
db_file_inc_playcount(int id);

void
db_file_ping(char *path);

char *
db_file_path_byid(int id);

int
db_file_id_bypath(char *path);

int
db_file_id_byfilebase(char *filename, char *base);

int
db_file_id_byfile(char *filename);

int
db_file_id_byurl(char *url);

time_t
db_file_stamp_bypath(char *path);

struct media_file_info *
db_file_fetch_byid(int id);

int
db_file_add(struct media_file_info *mfi);

int
db_file_update(struct media_file_info *mfi);

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

struct playlist_info *
db_pl_fetch_bypath(char *path);

struct playlist_info *
db_pl_fetch_bytitlepath(char *title, char *path);

int
db_pl_add(char *title, char *path, int *id);

int
db_pl_add_item_bypath(int plid, char *path);

int
db_pl_add_item_byid(int plid, int fileid);

void
db_pl_clear_items(int id);

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

enum group_type
db_group_type_byid(int id);

/* Remotes */
int
db_pairing_add(struct pairing_info *pi);

int
db_pairing_fetch_byguid(struct pairing_info *pi);

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
