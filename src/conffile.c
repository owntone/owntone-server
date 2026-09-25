/*
 * Copyright (C) 2009-2011 Julien BLACHE <jb@jblache.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#ifdef HAVE_CONFIG_H
# include <config.h>
#endif

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <ctype.h>

#include <sys/types.h>
#include <sys/utsname.h>
#include <pwd.h>

#include <errno.h>

#include <confuse.h>

#include "logger.h"
#include "misc.h"
#include "conffile.h"


/* Forward */
static int cb_loglevel(cfg_t *cfg, cfg_opt_t *opt, const char *value, void *result);

/* library directory alias section structure */
static cfg_opt_t sec_directory[] =
  {
    CFG_STR("alias", NULL, CFGF_NONE),
    CFG_END()
  };

/* general section structure */
static cfg_opt_t sec_general[] =
  {
    CFG_STR("uid", "nobody", CFGF_NONE),
    CFG_STR("db_path", STATEDIR "/cache/" PACKAGE "/songs3.db", CFGF_NONE),
    CFG_STR("db_backup_path", NULL, CFGF_NONE),
    CFG_STR("logfile", STATEDIR "/log/" PACKAGE ".log", CFGF_NONE),
    CFG_INT_CB("loglevel", E_LOG, CFGF_NONE, &cb_loglevel),
    CFG_STR("logformat", "default", CFGF_NONE),
    CFG_STR("admin_password", NULL, CFGF_NONE),
    CFG_INT("websocket_port", 3688, CFGF_NONE),
    CFG_STR("websocket_interface", NULL, CFGF_DEPRECATED),
    CFG_STR_LIST("trusted_networks", "{lan}", CFGF_NONE),
    CFG_BOOL("ipv6", cfg_false, CFGF_NONE),
    CFG_STR("bind_address", NULL, CFGF_NONE),
    CFG_STR("cache_dir", STATEDIR "/cache/" PACKAGE, CFGF_NONE),
    CFG_STR("cache_path", NULL, CFGF_DEPRECATED),
    CFG_INT("cache_daap_threshold", 1000, CFGF_NONE),
    CFG_BOOL("speaker_autoselect", cfg_false, CFGF_NONE),
#if defined(__FreeBSD__) || defined(__FreeBSD_kernel__)
    CFG_BOOL("high_resolution_clock", cfg_false, CFGF_NONE),
#else
    CFG_BOOL("high_resolution_clock", cfg_true, CFGF_NONE),
#endif
    // Hidden options
    CFG_INT("db_pragma_cache_size", -1, CFGF_NONE),
    CFG_STR("db_pragma_journal_mode", NULL, CFGF_NONE),
    CFG_INT("db_pragma_synchronous", -1, CFGF_NONE),
    CFG_STR("cache_daap_filename", "daap.db", CFGF_NONE),
    CFG_STR("cache_artwork_filename", "artwork.db", CFGF_NONE),
    CFG_STR("cache_xcode_filename", "xcode.db", CFGF_NONE),
    CFG_STR("allow_origin", "*", CFGF_NONE),
    CFG_STR("user_agent", PACKAGE_NAME "/" PACKAGE_VERSION, CFGF_NONE),
    CFG_BOOL("ssl_verifypeer", cfg_true, CFGF_NONE),
    CFG_BOOL("timer_test", cfg_false, CFGF_NONE),
    CFG_INT("start_buffer_ms", 2250, CFGF_NONE),
    CFG_END()
  };

/* library section structure */
static cfg_opt_t sec_library[] =
  {
    CFG_STR("name", "My Music on %h", CFGF_NONE),
    CFG_INT("port", 3689, CFGF_NONE),
    CFG_STR("password", NULL, CFGF_NONE),
    CFG_STR_LIST("directories", NULL, CFGF_NONE),
    CFG_SEC("directory", sec_directory, CFGF_MULTI | CFGF_TITLE),
    CFG_BOOL("follow_symlinks", cfg_true, CFGF_NONE),
    CFG_STR_LIST("podcasts", NULL, CFGF_NONE),
    CFG_STR_LIST("audiobooks", NULL, CFGF_NONE),
    CFG_STR_LIST("compilations", NULL, CFGF_NONE),
    CFG_STR("compilation_artist", NULL, CFGF_NONE),
    CFG_BOOL("hide_singles", cfg_false, CFGF_NONE),
    CFG_BOOL("radio_playlists", cfg_false, CFGF_NONE),
    CFG_STR("name_library", "Library", CFGF_NONE),
    CFG_STR("name_music", "Music", CFGF_NONE),
    CFG_STR("name_movies", "Movies", CFGF_NONE),
    CFG_STR("name_tvshows", "TV Shows", CFGF_NONE),
    CFG_STR("name_podcasts", "Podcasts", CFGF_NONE),
    CFG_STR("name_audiobooks", "Audiobooks", CFGF_NONE),
    CFG_STR("name_radio", "Radio", CFGF_NONE),
    CFG_STR("name_chapter", "Chapter", CFGF_NONE),
    CFG_STR("name_unknown_title", "Unknown title", CFGF_NONE),
    CFG_STR("name_unknown_artist", "Unknown artist", CFGF_NONE),
    CFG_STR("name_unknown_album", "Unknown album", CFGF_NONE),
    CFG_STR("name_unknown_genre", "Unknown genre", CFGF_NONE),
    CFG_STR("name_unknown_composer", "Unknown composer", CFGF_NONE),
    CFG_STR_LIST("artwork_basenames", "{artwork,cover,Folder}", CFGF_NONE),
    CFG_BOOL("artwork_individual", cfg_false, CFGF_NONE),
    CFG_STR_LIST("artwork_online_sources", NULL, CFGF_NONE),
    CFG_STR_LIST("filetypes_ignore", "{.db,.ini,.db-journal,.pdf,.metadata}", CFGF_NONE),
    CFG_STR_LIST("filepath_ignore", NULL, CFGF_NONE),
    CFG_BOOL("filescan_disable", cfg_false, CFGF_NONE),
    CFG_BOOL("m3u_overrides", cfg_false, CFGF_NONE),
    CFG_BOOL("itunes_overrides", cfg_false, CFGF_NONE),
    CFG_BOOL("itunes_smartpl", cfg_false, CFGF_NONE),
    CFG_STR_LIST("no_decode", NULL, CFGF_NONE),
    CFG_STR_LIST("force_decode", NULL, CFGF_NONE),
    CFG_STR("prefer_format", NULL, CFGF_NONE),
    CFG_BOOL("pipe_autostart", cfg_true, CFGF_NONE),
    CFG_INT("pipe_sample_rate", 44100, CFGF_NONE),
    CFG_INT("pipe_bits_per_sample", 16, CFGF_NONE),
    CFG_BOOL("rating_updates", cfg_false, CFGF_NONE),
    CFG_BOOL("read_rating", cfg_false, CFGF_NONE),
    CFG_BOOL("write_rating", cfg_false, CFGF_NONE),
    CFG_INT("max_rating", 100, CFGF_NONE),
    CFG_BOOL("allow_modifying_stored_playlists", cfg_false, CFGF_NONE),
    CFG_STR("default_playlist_directory", NULL, CFGF_NONE),
    CFG_BOOL("clear_queue_on_stop_disable", cfg_false, CFGF_NONE),
    CFG_BOOL("only_first_genre", cfg_false, CFGF_NONE),
    CFG_STR_LIST("decode_audio_filters", NULL, CFGF_NONE),
    CFG_STR_LIST("decode_video_filters", NULL, CFGF_NONE),
    CFG_END()
  };

/* local audio section structure */
static cfg_opt_t sec_audio[] =
  {
    CFG_STR("nickname", "Computer", CFGF_NONE),
    CFG_STR("type", NULL, CFGF_NONE),
    CFG_STR("server", NULL, CFGF_NONE),
    CFG_STR("card", "default", CFGF_NONE),
    CFG_STR("mixer", NULL, CFGF_NONE),
    CFG_STR("mixer_device", NULL, CFGF_NONE),
    CFG_BOOL("sync_disable", cfg_false, CFGF_NONE),
    CFG_INT("offset", 0, CFGF_DEPRECATED),
    CFG_INT("offset_ms", 0, CFGF_DEPRECATED),
    CFG_INT("adjust_period_seconds", 100, CFGF_NONE),
    // Hidden options
    CFG_BOOL("exclusive", cfg_false, CFGF_NONE),
    CFG_END()
  };

/* local ALSA audio section structure */
static cfg_opt_t sec_alsa[] =
  {
    CFG_STR("nickname", NULL, CFGF_NONE),
    CFG_STR("mixer", NULL, CFGF_NONE),
    CFG_STR("mixer_device", NULL, CFGF_NONE),
    CFG_INT("offset_ms", 0, CFGF_DEPRECATED),
    // Hidden options
    CFG_BOOL("exclusive", cfg_false, CFGF_NONE),
    CFG_END()
  };

/* AirPlay/ApEx shared section structure */
static cfg_opt_t sec_airplay_shared[] =
  {
    CFG_INT("control_port", 0, CFGF_NONE),
    CFG_INT("timing_port", 0, CFGF_NONE),
    CFG_BOOL("uncompressed_alac", cfg_false, CFGF_NONE),
    // HomePod OS 27 has user-agent validation, see issue #2042
    CFG_STR("user_agent", "AirPlay/999.0.0", CFGF_NONE),
    CFG_END()
  };

/* AirPlay/ApEx device section structure */
static cfg_opt_t sec_airplay[] =
  {
    CFG_INT("max_volume", 11, CFGF_NONE),
    CFG_BOOL("exclude", cfg_false, CFGF_NONE),
    CFG_BOOL("permanent", cfg_false, CFGF_NONE),
    CFG_BOOL("reconnect", cfg_false, CFGF_NODEFAULT),
    CFG_STR("password", NULL, CFGF_NONE),
    CFG_BOOL("raop_disable", cfg_false, CFGF_DEPRECATED),
    CFG_BOOL("airplay2_disable", cfg_false, CFGF_NONE),
    CFG_STR("nickname", NULL, CFGF_NONE),
    // Hidden options
    CFG_BOOL("exclusive", cfg_false, CFGF_NONE),
    CFG_BOOL("ptp_disable", cfg_false, CFGF_NONE),
    CFG_END()
  };

/* Chromecast device section structure */
static cfg_opt_t sec_chromecast[] =
  {
    CFG_INT("max_volume", 11, CFGF_NONE),
    CFG_BOOL("exclude", cfg_false, CFGF_NONE),
    CFG_INT("offset_ms", 0, CFGF_DEPRECATED),
    CFG_STR("nickname", NULL, CFGF_NONE),
    // Hidden options
    CFG_BOOL("exclusive", cfg_false, CFGF_NONE),
    CFG_END()
  };

/* FIFO section structure */
static cfg_opt_t sec_fifo[] =
  {
    CFG_STR("nickname", "fifo", CFGF_NONE),
    CFG_STR("path", NULL, CFGF_NONE),
    // Hidden options
    CFG_BOOL("exclusive", cfg_false, CFGF_NONE),
    CFG_END()
  };

/* RCP/Soundbridge section structure */
static cfg_opt_t sec_rcp[] =
  {
    CFG_BOOL("exclude", cfg_false, CFGF_NONE),
    CFG_BOOL("clear_on_close", cfg_false, CFGF_NONE),
    // Hidden options
    CFG_BOOL("exclusive", cfg_false, CFGF_NONE),
    CFG_END()
  };

/* Spotify section structure */
static cfg_opt_t sec_spotify[] =
  {
    CFG_BOOL("use_libspotify", cfg_false, CFGF_DEPRECATED),
    CFG_STR("settings_dir", STATEDIR "/cache/" PACKAGE "/libspotify", CFGF_DEPRECATED),
    CFG_STR("cache_dir", "/tmp", CFGF_DEPRECATED),
    CFG_INT("bitrate", 0, CFGF_NONE),
    CFG_BOOL("base_playlist_disable", cfg_false, CFGF_NONE),
    CFG_BOOL("artist_override", cfg_false, CFGF_NONE),
    CFG_BOOL("album_override", cfg_false, CFGF_NONE),
    CFG_BOOL("disable_legacy_mode", cfg_false, CFGF_NONE),
    CFG_BOOL("initscan_disable", cfg_false, CFGF_NONE),
    // Issued by Spotify on developer.spotify.com for forked-daapd/OwnTone
    CFG_STR("webapi_client_id", "0e684a5422384114a8ae7ac020f01789", CFGF_NONE),
    CFG_STR("webapi_client_secret", "232af95f39014c9ba218285a5c11a239", CFGF_NONE),
    // Must be in allow-list on developer.spotify.com for forked-daapd/OwnTone
    CFG_STR("redirect_uri", "https://owntone.github.io/owntone-oauth/spotify/", CFGF_NONE),
    CFG_END()
  };

/* SQLite section structure */
static cfg_opt_t sec_sqlite[] =
  {
    CFG_INT("pragma_cache_size_library", -1, CFGF_NONE),
    CFG_INT("pragma_cache_size_cache", -1, CFGF_NONE),
    CFG_STR("pragma_journal_mode", NULL, CFGF_NONE),
    CFG_INT("pragma_synchronous", -1, CFGF_NONE),
    CFG_INT("pragma_mmap_size_library", -1, CFGF_NONE),
    CFG_INT("pragma_mmap_size_cache", -1, CFGF_NONE),
    CFG_BOOL("vacuum", cfg_true, CFGF_NONE),
    CFG_END()
  };

/* MPD section structure */
static cfg_opt_t sec_mpd[] =
  {
    CFG_INT("port", 6600, CFGF_NONE),
    CFG_INT("http_port", 0, CFGF_NONE),
    CFG_BOOL("enable_httpd_plugin", cfg_false, CFGF_NONE),
    CFG_BOOL("clear_queue_on_stop_disable", cfg_false, CFGF_NODEFAULT | CFGF_DEPRECATED),
    CFG_BOOL("allow_modifying_stored_playlists", cfg_false, CFGF_NODEFAULT | CFGF_DEPRECATED),
    CFG_STR("default_playlist_directory", NULL, CFGF_NODEFAULT | CFGF_DEPRECATED),
    CFG_END()
  };

/* streaming section structure */
static cfg_opt_t sec_streaming[] =
  {
    CFG_INT("sample_rate", 44100, CFGF_NONE),
    CFG_INT("bit_rate", 192, CFGF_NONE),
    CFG_INT("icy_metaint", 16384, CFGF_NONE),
    // Hidden options
    CFG_BOOL("exclusive", cfg_false, CFGF_NONE),
    CFG_END()
  };

/* Config file structure */
static cfg_opt_t toplvl_cfg[] =
  {
    CFG_SEC("general", sec_general, CFGF_NONE),
    CFG_SEC("library", sec_library, CFGF_NONE),
    CFG_SEC("audio", sec_audio, CFGF_NONE),
    CFG_SEC("alsa", sec_alsa, CFGF_MULTI | CFGF_TITLE),
    CFG_SEC("airplay_shared", sec_airplay_shared, CFGF_NONE),
    CFG_SEC("airplay", sec_airplay, CFGF_MULTI | CFGF_TITLE),
    CFG_SEC("chromecast", sec_chromecast, CFGF_MULTI | CFGF_TITLE),
    CFG_SEC("fifo", sec_fifo, CFGF_NONE),
    CFG_SEC("rcp", sec_rcp, CFGF_MULTI | CFGF_TITLE),
    CFG_SEC("spotify", sec_spotify, CFGF_NONE),
    CFG_SEC("sqlite", sec_sqlite, CFGF_NONE),
    CFG_SEC("mpd", sec_mpd, CFGF_NONE),
    CFG_SEC("streaming", sec_streaming, CFGF_NONE),
    CFG_END()
  };

cfg_t *cfg;
uint64_t libhash;
uid_t runas_uid;
gid_t runas_gid;

/* Aliases configured via the library "directory" sections. The scanner keys on
 * both the path as written in the config file and its dereferenced version,
 * because bulk_scan() uses the former for the parent directories and the
 * latter for everything below it.
 */
struct dir_alias
{
  char *path;
  char *real_path;
  char *alias;

  struct dir_alias *next;
};

static struct dir_alias *dir_aliases;


static void
logger_confuse(cfg_t *config, const char *format, va_list args)
{
  char fmt[80];

  if (config && config->name && config->line)
    snprintf(fmt, sizeof(fmt), "[%s:%d] %s\n", config->name, config->line, format);
  else
    snprintf(fmt, sizeof(fmt), "%s\n", format);

  DVPRINTF(E_LOG, L_CONF, fmt, args);
}

static int
cb_loglevel(cfg_t *config, cfg_opt_t *opt, const char *value, void *result)
{
  if (strcasecmp(value, "fatal") == 0)
    *(long int *)result = E_FATAL;
  else if (strcasecmp(value, "log") == 0)
    *(long int *)result = E_LOG;
  else if (strcasecmp(value, "warning") == 0)
    *(long int *)result = E_WARN;
  else if (strcasecmp(value, "info") == 0)
    *(long int *)result = E_INFO;
  else if (strcasecmp(value, "debug") == 0)
    *(long int *)result = E_DBG;
  else if (strcasecmp(value, "spam") == 0)
    *(long int *)result = E_SPAM;
  else
    {
      DPRINTF(E_WARN, L_CONF, "Unrecognised loglevel '%s'\n", value);
      /* Default to warning */
      *(long int *)result = 1;
    }

  return 0;
}

// Makes sure cache_dir ends with a slash
static int
sanitize_cache_dir(cfg_t *general)
{
  char *dir;
  const char *s;
  char *appended;
  size_t len;

  dir = cfg_getstr(general, "cache_dir");
  len = strlen(dir);

  s = strrchr(dir, '/');
  if (s && (s + 1 == dir + len))
    return 0;

  appended = safe_asprintf("%s/", dir);

  cfg_setstr(general, "cache_dir", appended);

  free(appended);

  return 0;
}

static int
conffile_expand_libname(cfg_t *lib)
{
  char *libname;
  char *hostname;
  char *s;
  char *d;
  char *expanded;
  struct utsname sysinfo;
  size_t len;
  size_t olen;
  size_t hostlen;
  size_t verlen;
  int ret;

  libname = cfg_getstr(lib, "name");
  olen = strlen(libname);

  /* Fast path */
  s = strchr(libname, '%');
  if (!s)
    {
      libhash = murmur_hash64(libname, olen, 0);
      return 0;
    }

  /* Grab what we need */
  ret = uname(&sysinfo);
  if (ret != 0)
    {
      DPRINTF(E_WARN, L_CONF, "Could not get system name: %s\n", strerror(errno));
      hostname = "Unknown host";
    }
  else
    hostname = sysinfo.nodename;

  hostlen = strlen(hostname);
  verlen = strlen(VERSION);

  /* Compute expanded size */
  len = olen;
  s = libname;
  while (*s)
    {
      if (*s == '%')
	{
	  s++;

	  switch (*s)
	    {
	      case 'h':
		len += hostlen;
		break;

	      case 'v':
		len += verlen;
		break;
	    }
	}
      s++;
    }

  expanded = (char *)malloc(len + 1);
  if (!expanded)
    {
      DPRINTF(E_FATAL, L_CONF, "Out of memory\n");

      return -1;
    }
  memset(expanded, 0, len + 1);

  /* Do the actual expansion */
  s = libname;
  d = expanded;
  while (*s)
    {
      if (*s == '%')
	{
	  s++;

	  switch (*s)
	    {
	      case 'h':
		strcat(d, hostname);
		d += hostlen;
		break;

	      case 'v':
		strcat(d, VERSION);
		d += verlen;
		break;
	    }

	  s++;
	}
      else
	{
	  *d = *s;

	  s++;
	  d++;
	}
    }

  cfg_setstr(lib, "name", expanded);

  libhash = murmur_hash64(expanded, strlen(expanded), 0);

  free(expanded);

  return 0;
}

/* Returns the length of prefix within path if prefix is path itself or one of
 * its parent directories, otherwise 0. A trailing slash on prefix is ignored,
 * the config file may or may not have one.
 */
static size_t
path_prefix_len(const char *path, const char *prefix)
{
  size_t len;

  if (!prefix)
    return 0;

  len = strlen(prefix);
  while (len > 1 && prefix[len - 1] == '/')
    len--;

  if (len == 0 || strncmp(path, prefix, len) != 0)
    return 0;

  if (path[len] != '\0' && path[len] != '/')
    return 0;

  return len;
}

/* Returns the length of the first path component of path, e.g. 3 for "/usr" or
 * "/usr/share". Used to detect aliases that would collide with a library
 * directory that has no alias of its own.
 */
static size_t
path_first_component_len(const char *path)
{
  const char *sep;

  while (*path == '/')
    path++;

  sep = strchr(path, '/');

  return sep ? (size_t)(sep - path) : strlen(path);
}

static int
alias_validate(cfg_t *lib, const char *path, const char *alias)
{
  const char *dir;
  const char *component;
  size_t i;
  size_t ndirs;
  bool found;

  if (!alias || alias[0] == '\0')
    {
      DPRINTF(E_FATAL, L_CONF, "Empty alias for library directory '%s'\n", path);
      return -1;
    }

  if (strchr(alias, '/'))
    {
      DPRINTF(E_FATAL, L_CONF, "Alias '%s' for library directory '%s' must not contain '/'\n", alias, path);
      return -1;
    }

  if (strcmp(alias, ".") == 0 || strcmp(alias, "..") == 0)
    {
      DPRINTF(E_FATAL, L_CONF, "Alias '%s' for library directory '%s' is not a valid name\n", alias, path);
      return -1;
    }

  if (isspace((unsigned char)alias[0]) || isspace((unsigned char)alias[strlen(alias) - 1]))
    {
      DPRINTF(E_FATAL, L_CONF, "Alias '%s' for library directory '%s' has leading or trailing whitespace\n", alias, path);
      return -1;
    }

  /* The alias must belong to a directory we actually scan, otherwise a typo
   * would silently do nothing.
   */
  ndirs = cfg_size(lib, "directories");
  found = false;
  for (i = 0; i < ndirs && !found; i++)
    {
      if (strcmp(cfg_getnstr(lib, "directories", i), path) == 0)
	found = true;
    }

  if (!found)
    {
      DPRINTF(E_FATAL, L_CONF, "Alias configured for '%s', which is not one of the library directories\n", path);
      return -1;
    }

  /* An alias sits directly below /file:, so it can collide with the first path
   * component of a library directory that is not aliased.
   */
  for (i = 0; i < ndirs; i++)
    {
      dir = cfg_getnstr(lib, "directories", i);
      if (strcmp(dir, path) == 0 || cfg_gettsec(lib, "directory", dir))
	continue;

      component = dir + strspn(dir, "/");
      if (strlen(alias) == path_first_component_len(dir) && strncmp(alias, component, strlen(alias)) == 0)
	{
	  DPRINTF(E_FATAL, L_CONF, "Alias '%s' collides with library directory '%s'\n", alias, dir);
	  return -1;
	}
    }

  return 0;
}

static void
alias_free(void)
{
  struct dir_alias *da;

  while (dir_aliases)
    {
      da = dir_aliases;
      dir_aliases = da->next;

      free(da->path);
      free(da->real_path);
      free(da->alias);
      free(da);
    }
}

static int
alias_init(cfg_t *lib)
{
  cfg_t *sec;
  struct dir_alias *da;
  const char *path;
  const char *alias;
  size_t i;
  size_t nsecs;

  nsecs = cfg_size(lib, "directory");
  for (i = 0; i < nsecs; i++)
    {
      sec = cfg_getnsec(lib, "directory", i);
      path = cfg_title(sec);
      alias = cfg_getstr(sec, "alias");

      if (alias_validate(lib, path, alias) < 0)
	goto error;

      for (da = dir_aliases; da; da = da->next)
	{
	  if (strcmp(da->path, path) == 0)
	    {
	      DPRINTF(E_FATAL, L_CONF, "Multiple 'directory' sections for '%s'\n", path);
	      goto error;
	    }

	  if (strcmp(da->alias, alias) == 0)
	    {
	      DPRINTF(E_FATAL, L_CONF, "Alias '%s' is used for both '%s' and '%s'\n", alias, da->path, path);
	      goto error;
	    }

	  /* Nested aliases would make the mapping ambiguous. Listing a library
	   * directory inside another one is pointless anyway.
	   */
	  if (path_prefix_len(path, da->path) || path_prefix_len(da->path, path))
	    {
	      DPRINTF(E_FATAL, L_CONF, "Aliased library directories '%s' and '%s' are nested\n", da->path, path);
	      goto error;
	    }
	}

      CHECK_NULL(L_CONF, da = calloc(1, sizeof(struct dir_alias)));
      CHECK_NULL(L_CONF, da->path = strdup(path));
      CHECK_NULL(L_CONF, da->alias = strdup(alias));

      /* May legitimately fail if the directory is not mounted yet */
      da->real_path = realpath(path, NULL);

      da->next = dir_aliases;
      dir_aliases = da;
    }

  return 0;

 error:
  alias_free();

  return -1;
}

int
conffile_alias_apply(char *buf, size_t buflen, const char *path)
{
  struct dir_alias *da;
  size_t len;
  int ret;

  for (da = dir_aliases; da; da = da->next)
    {
      len = path_prefix_len(path, da->path);
      if (len == 0)
	len = path_prefix_len(path, da->real_path);

      if (len > 0)
	{
	  ret = snprintf(buf, buflen, "/%s%s", da->alias, path + len);
	  return ((ret < 0) || ((size_t)ret >= buflen)) ? -1 : 0;
	}
    }

  ret = snprintf(buf, buflen, "%s", path);

  return ((ret < 0) || ((size_t)ret >= buflen)) ? -1 : 0;
}

char *
conffile_alias_resolve(const char *vpath)
{
  struct dir_alias *da;
  size_t len;

  if (vpath[0] != '/')
    return NULL;

  for (da = dir_aliases; da; da = da->next)
    {
      len = strlen(da->alias);

      if (strncmp(vpath + 1, da->alias, len) != 0)
	continue;
      if (vpath[1 + len] != '\0' && vpath[1 + len] != '/')
	continue;

      /* Dereferenced, because that is what the scanner stores in the database */
      return safe_asprintf("%s%s", da->real_path ? da->real_path : da->path, vpath + 1 + len);
    }

  return NULL;
}

const char *
conffile_alias_get(const char *path)
{
  struct dir_alias *da;

  for (da = dir_aliases; da; da = da->next)
    {
      if (strcmp(da->path, path) == 0)
	return da->alias;
    }

  return NULL;
}

int
conffile_load(char *file)
{
  cfg_t *lib;
  struct passwd *pw;
  char *runas;
  int ret;

  cfg = cfg_init(toplvl_cfg, CFGF_NONE);

  cfg_set_error_function(cfg, logger_confuse);

  ret = cfg_parse(cfg, file);

  if (ret == CFG_FILE_ERROR)
    {
      DPRINTF(E_FATAL, L_CONF, "Could not open config file %s\n", file);

      goto out_fail;
    }
  else if (ret == CFG_PARSE_ERROR)
    {
      DPRINTF(E_FATAL, L_CONF, "Parse error in config file %s\n", file);

      goto out_fail;
    }

  /* Resolve runas username */
  runas = cfg_getstr(cfg_getsec(cfg, "general"), "uid");
  pw = getpwnam(runas);
  if (!pw)
    {
      DPRINTF(E_FATAL, L_CONF, "Could not lookup user %s: %s\n", runas, strerror(errno));

      goto out_fail;
    }

  runas_uid = pw->pw_uid;
  runas_gid = pw->pw_gid;

  ret = sanitize_cache_dir(cfg_getsec(cfg, "general"));
  if (ret != 0)
    {
      DPRINTF(E_FATAL, L_CONF, "Invalid configuration of cache_dir\n");

      goto out_fail;
    }

  lib = cfg_getsec(cfg, "library");

  if (cfg_size(lib, "directories") == 0)
    {
      DPRINTF(E_FATAL, L_CONF, "No directories specified for library\n");

      goto out_fail;
    }

  ret = alias_init(lib);
  if (ret != 0)
    {
      DPRINTF(E_FATAL, L_CONF, "Invalid configuration of library directory aliases\n");

      goto out_fail;
    }

  /* Do keyword expansion on library names */
  ret = conffile_expand_libname(lib);
  if (ret != 0)
    {
      DPRINTF(E_FATAL, L_CONF, "Could not expand library name\n");

      goto out_fail;
    }

  return 0;

 out_fail:
  cfg_free(cfg);

  return -1;
}

void
conffile_unload(void)
{
  alias_free();

  cfg_free(cfg);
}
