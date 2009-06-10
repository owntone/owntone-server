
#ifndef __DB_H__
#define __DB_H__

#include <time.h>

#include <stdint.h>

#include <sqlite3.h>


enum index_type {
  I_NONE,
  I_FIRST,
  I_LAST,
  I_SUB
};

#define Q_F_BROWSE (1 << 15)

enum query_type {
  Q_ITEMS            = (1 << 0),
  Q_PL               = (1 << 1),
  Q_PLITEMS          = (1 << 2),
  Q_BROWSE_ARTISTS   = Q_F_BROWSE | (1 << 3),
  Q_BROWSE_ALBUMS    = Q_F_BROWSE | (1 << 4),
  Q_BROWSE_GENRES    = Q_F_BROWSE | (1 << 5),
  Q_BROWSE_COMPOSERS = Q_F_BROWSE | (1 << 6)
};

struct query_params {
  /* Query parameters, filled in by caller */
  enum query_type type;
  enum index_type idx_type;
  int pl_id;
  int offset;
  int limit;

  char *filter;

  /* Query results, filled in by query_start */
  int results;

  /* Private query context, keep out */
  sqlite3_stmt *stmt;
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
  uint64_t file_size;
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
  uint32_t force_update;
  uint64_t sample_count;
  char compilation;

  /* iTunes 5+ */
  uint32_t contentrating;

  /* iTunes 6.0.2 */
  uint32_t has_video;
  uint32_t bits_per_sample;

  char *album_artist;
};

struct playlist_info {
  uint32_t id;           /* integer id (miid) */
  char *title;           /* playlist name as displayed in iTunes (minm) */
  uint32_t type;         /* see PL_ types (deprecated) */
  uint32_t items;        /* number of items (mimc) */
  char *query;           /* where clause if type 1 (MSPS) */
  uint32_t db_timestamp; /* time last updated */
  uint32_t disabled;
  char *path;            /* path of underlying playlist */
  uint32_t index;        /* index of playlist for paths with multiple playlists */
};

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
};

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
  char *force_update;
  char *codectype;
  char *idx;
  char *has_video;
  char *contentrating;
  char *bits_per_sample;
  char *album_artist;
};

struct watch_info {
  int wd;
  char *path;
  uint32_t cookie;
  int toplevel;
  int libidx;
};


char *
db_escape_string(const char *str);

void
free_mfi(struct media_file_info *mfi, int content_only);

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
db_query_fetch_string(struct query_params *qp, char **string);

/* Files */
int
db_files_get_count(int *count);

void
db_file_inc_playcount(int id);

void
db_file_ping(int id);

int
db_file_id_bypath(char *path, int *id);

struct media_file_info *
db_file_fetch_byid(int id);

struct media_file_info *
db_file_fetch_bypath(char *path);

int
db_file_add(struct media_file_info *mfi);

int
db_file_update(struct media_file_info *mfi);

/* Playlists */
int
db_pl_get_count(int *count);

void
db_pl_ping(int id);

struct playlist_info *
db_pl_fetch_bypath(char *path);

int
db_pl_add(char *title, char *path, int *id);

int
db_pl_add_item(int plid, int mfid);

void
db_pl_update(int id);

void
db_pl_update_all(void);

void
db_pl_delete(int id);

/* Inotify */
int
db_watch_clear(void);

int
db_watch_add(struct watch_info *wi);

int
db_watch_delete_bywd(struct watch_info *wi);

int
db_watch_get_bywd(struct watch_info *wi);


int
db_perthread_init(void);

void
db_perthread_deinit(void);

int
db_init(void);

#endif /* !__DB_H__ */
