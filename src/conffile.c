/*
 * Copyright (C) 2009 Julien BLACHE <jb@jblache.org>
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
# include "config.h"
#endif

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include <sys/utsname.h>

#include <errno.h>

#include <confuse.h>

#include "daapd.h"
#include "err.h"
#include "conffile.h"


/* Forward */
static int cb_loglevel(cfg_t *cfg, cfg_opt_t *opt, const char *value, void *result);

/* general section structure */
static cfg_opt_t sec_general[] =
  {
    CFG_STR("uid", "nobody", CFGF_NONE),
    CFG_STR("admin_password", NULL, CFGF_NONE),
    CFG_STR("logfile", "/var/log/mt-daapd.log", CFGF_NONE),
    CFG_INT_CB("loglevel", E_LOG, CFGF_NONE, &cb_loglevel),
    CFG_END()
  };

/* library section structure */
static cfg_opt_t sec_library[] =
  {
    CFG_STR("name", "%l on %h", CFGF_NONE),
    CFG_INT("port", 3689, CFGF_NONE),
    CFG_STR("password", NULL, CFGF_NONE),
    CFG_STR_LIST("directories", NULL, CFGF_NONE),
    CFG_STR_LIST("compilations", NULL, CFGF_NONE),
    CFG_STR_LIST("no_transcode", NULL, CFGF_NONE),
    CFG_STR_LIST("force_transcode", NULL, CFGF_NONE),
    CFG_END()
  };

/* Config file structure */
static cfg_opt_t toplvl_cfg[] =
  {
    CFG_SEC("general", sec_general, CFGF_NONE),
    CFG_SEC("library", sec_library, CFGF_MULTI | CFGF_TITLE),
    CFG_END()
  };

cfg_t *cfg;


static int
cb_loglevel(cfg_t *cfg, cfg_opt_t *opt, const char *value, void *result)
{
  if (strcasecmp(value, "fatal") == 0)
    *(long int *)result = E_FATAL;
  else if (strcasecmp(value, "log") == 0)
    *(long int *)result = E_LOG;
  else if (strcasecmp(value, "warning") == 0)
    *(long int *)result = E_WARN;
  else if (strcasecmp(value, "info") == 0)
    *(long int *)result = E_INF;
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

static int
conffile_expand_libname(cfg_t *lib)
{
  char *libname;
  const char *title;
  char *hostname;
  char *s;
  char *d;
  char *expanded;
  struct utsname sysinfo;
  size_t len;
  size_t olen;
  size_t titlelen;
  size_t hostlen;
  size_t verlen;
  int ret;

  libname = cfg_getstr(lib, "name");

  /* Fast path */
  s = strchr(libname, '%');
  if (!s)
    return 0;

  /* Grab what we need */
  title = cfg_title(lib);

  ret = uname(&sysinfo);
  if (ret != 0)
    {
      DPRINTF(E_WARN, L_CONF, "Could not get system name: %s\n", strerror(errno));
      hostname = "Unknown host";
    }
  else
    hostname = sysinfo.nodename;

  titlelen = strlen(title);
  hostlen = strlen(hostname);
  verlen = strlen(VERSION);

  olen = strlen(libname);
  len = olen;

  /* Compute expanded size */
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

	      case 'l':
		len += titlelen;
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

	      case 'l':
		strcat(d, title);
		d += titlelen;
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

  free(expanded);

  return 0;
}


int
conffile_load(char *file)
{
  cfg_t *lib;
  int nlib;
  int *libports;
  int libport;
  int error;
  int i;
  int j;
  int ret;

  cfg = cfg_init(toplvl_cfg, CFGF_NONE);

  ret = cfg_parse(cfg, file);

  if (ret == CFG_FILE_ERROR)
    {
      DPRINTF(E_FATAL, L_CONF, "Could not open config file %s\n", file);

      cfg_free(cfg);
      return -1;
    }
  else if (ret == CFG_PARSE_ERROR)
    {
      DPRINTF(E_FATAL, L_CONF, "Parse error in config file %s\n", file);

      cfg_free(cfg);
      return -1;
    }

  nlib = cfg_size(cfg, "library");

  DPRINTF(E_INF, L_CONF, "%d music libraries configured\n", nlib);

  libports = (int *)malloc(nlib * sizeof(int));
  memset(libports, 0, (nlib * sizeof(int)));

  error = 0;
  for (i = 0; i < nlib; i++)
    {
      lib = cfg_getnsec(cfg, "library", i);
      libport = cfg_getint(lib, "port");

      error = ((libport > 65535) || (libport < 1024));
      if (error)
	{
	  DPRINTF(E_FATAL, L_CONF, "Invalid port number for library '%s', must be between 1024 and 65535\n",
		  cfg_title(lib));

	  break;
	}

      /* Check libraries ports */
      for (j = 0; j < i; j++)
	{
	  error = (libports[j] == libport);
	  if (error)
	    break;
	}

      if (error)
	{
	  DPRINTF(E_FATAL, L_CONF, "Port collision for library '%s' and library '%s'\n",
		  cfg_title(cfg_getnsec(cfg, "library", j)), cfg_title(lib));
	  DPRINTF(E_FATAL, L_CONF, "Port numbers must be unique accross all libraries\n");

	  break;
	}

      libports[i] = libport;

      error = (cfg_size(lib, "directories") == 0);
      if (error)
	{
	  DPRINTF(E_FATAL, L_CONF, "No directories specified for library '%s'\n",
		  cfg_title(lib));

	  break;
	}

      /* Do keyword expansion on library names */
      ret = conffile_expand_libname(lib);
      if (ret != 0)
	{
	  DPRINTF(E_FATAL, L_CONF, "Could not expand library name\n");

	  free(libports);
	  cfg_free(cfg);
	  return -1;
	}
    }

  free(libports);

  if (error)
    {
      cfg_free(cfg);

      return -1;
    }

  return 0;
}

void
conffile_unload(void)
{
  cfg_free(cfg);
}


/* Wrappers for old configfile handling */
#include "conf.h"

int
conf_get_int(char *section, char *key, int dflt)
{
  cfg_t *lib;

  if ((strcmp(section, "general") == 0)
      && (strcmp(key, "port") == 0))
    {
      lib = cfg_getnsec(cfg, "library", 0);
      return cfg_getint(lib, "port");
    }

  /* Return default for everything else */
  return dflt;
}

int conf_get_string(char *section, char *key, char *dflt, char *out, int *size)
{
  cfg_t *lib;
  size_t len;
  char *val = NULL;

  if (strcmp(section, "general") == 0)
    {
      if (strcmp(key, "admin_pw") == 0)
	{
	  val = cfg_getstr(cfg_getsec(cfg, "general"), "admin_password");
	}
      else if (strcmp(key, "password") == 0)
	{
	  lib = cfg_getnsec(cfg, "library", 0);
	  val = cfg_getstr(lib, "password");
	}
      else if (strcmp(key, "extensions") == 0)
	{
	  val = ".mp3,.m4a,.m4p,.ogg,.flac,.mpc";
	}
      else if (strcmp(key, "runas") == 0)
	{
	  val = cfg_getstr(cfg_getsec(cfg, "general"), "uid");
	}
      else if (strcmp(key, "db_type") == 0)
	{
	  val = "sqlite3";
	}
      else if (strcmp(key, "db_parms") == 0)
	{
	  val = "/var/cache/mt-daapd";
	}
      /* web_root now static */
      else if (strcmp(key, "web_root") == 0)
	{
	  val = "/usr/share/mt-daapd/admin-root";
	}
    }
  else if (strcmp(section, "plugins") == 0)
    {
      /* plugin_dir now static, going away */
      if (strcmp(key, "plugin_dir") == 0)
	{
	  val = "/usr/lib/mt-daapd/plugins";
	}
    }

  /* Default value */
  if (val == NULL)
    val = dflt;

  if (val == NULL)
    {
      *size = 0;
      return 0;
    }

  len = strlen(val) + 1;
  if (len <= *size)
    {
      strcpy(out, val);
      *size = len;
    }
  else
    {
      *size = len;
      return CONF_E_OVERFLOW;
    }

  return 0;
}

char *
conf_alloc_string(char *section, char *key, char *dflt)
{
  char *val;
  int len;
  int ret;

  len = 256;
  val = (char *)malloc(len);
  if (!val)
    return NULL;

  memset(val, 0, len);

  ret = conf_get_string(section, key, dflt, val, &len);
  if (ret != 0)
    return NULL;

  if (len == 0)
    {
      free(val);
      val = NULL;
    }

  return val;
}

int
conf_get_array(char *section, char *key, char ***argvp)
{
  cfg_t *lib;
  int nelem;
  char *opt;
  char **a;
  int i;

  lib = cfg_getnsec(cfg, "library", 0);

  if (strcmp(section, "general") == 0)
    {
      if (strcmp(key, "mp3_dir") == 0)
	{
	  nelem = cfg_size(lib, "directories");
	  if (nelem == 0)
	    return 0;

	  opt = "directories";
	}
      else if (strcmp(key, "compdirs") == 0)
	{
	  nelem = cfg_size(lib, "compilations");
	  if (nelem == 0)
	    return 0;

	  opt = "compilations";
	}
      else
	{
	  return 0;
	}
    }
  else
    return 0;

  a = (char **)malloc((nelem + 1) * sizeof(char *));
  if (!a)
    return 0;

  memset(a, 0, (nelem + 1) * sizeof(char *));

  for (i = 0; i < nelem; i++)
    {
      a[i] = strdup(cfg_getnstr(lib, opt, i));
    }

  *argvp = a;

  return 1;
}

void
conf_dispose_array(char **argv)
{
  int i;

  for (i = 0; argv[i]; i++)
    free(argv[i]);

  free(argv);
}

char *
conf_get_servername(void)
{
  cfg_t *lib;

  lib = cfg_getnsec(cfg, "library", 0);
  return strdup(cfg_getstr(lib, "name"));
}

/* Not supported */

int
conf_reload(void)
{
  /* Not supported */
  return CONF_E_NOTSUPP;
}

int
conf_set_int(char *section, char *key, int value, int verify)
{
  /* Not supported */
  return CONF_E_NOTSUPP;
}

int
conf_set_string(char *section, char *key, char *value, int verify)
{
  /* Not supported */
  return CONF_E_NOTSUPP;
}

int
conf_iswritable(void)
{
  /* Not supported */
  return 0;
}

int
conf_isset(char *section, char *key)
{
  /* Not pertinent */
  return 1;
}

int
conf_write(void)
{
  /* Not supported */
  return CONF_E_NOTSUPP;
}

char *
conf_get_filename(void)
{
  return "NOT SUPPORTED";
}
