/*
 * Copyright (C) 2015 Espen Jürgensen <espenjurgensen@gmail.com>
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
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <limits.h>
#include <sys/param.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <errno.h>

#include "icy.h"
#include "logger.h"
#include "misc.h"

#include <libavutil/opt.h>

static int
metadata_packet_get(struct icy_metadata *metadata, AVFormatContext *fmtctx)
{
  uint8_t *buffer;
  char *icy_token;
  char *ptr;
  char *end;

  av_opt_get(fmtctx, "icy_metadata_packet", AV_OPT_SEARCH_CHILDREN, &buffer);
  if (!buffer)
    return -1;

  icy_token = strtok((char *)buffer, ";");
  while (icy_token != NULL)
    {
      ptr = strchr(icy_token, '=');
      if (!ptr || (ptr[1] == '\0'))
	{
	  icy_token = strtok(NULL, ";");
	  continue;
	}

      ptr++;
      if (ptr[0] == '\'')
	ptr++;

      end = strrchr(ptr, '\'');
      if (end)
        *end = '\0';

      if (strncmp(icy_token, "StreamTitle", strlen("StreamTitle")) == 0)
	{
	  metadata->title = ptr;

	  /* Dash separates artist from title, if no dash assume all is title */
	  ptr = strstr(ptr, " - ");
	  if (ptr)
	    {
	      *ptr = '\0';
	      metadata->title = strdup(metadata->title);
	      *ptr = ' ';

	      metadata->artist = strdup(ptr + 3);
	    }
	  else
	    metadata->title = strdup(metadata->title);
	}
      else if (strncmp(icy_token, "StreamUrl", strlen("StreamUrl")) == 0)
	metadata->artwork_url = strdup(ptr);

      if (end)
	*end = '\'';

      icy_token = strtok(NULL, ";");
    }
  av_free(buffer);

  if (metadata->title)
    metadata->hash = djb_hash(metadata->title, strlen(metadata->title));

  return 0;
}

static int
metadata_header_get(struct icy_metadata *metadata, AVFormatContext *fmtctx)
{
  uint8_t *buffer;
  char *icy_token;
  char *ptr;

  av_opt_get(fmtctx, "icy_metadata_headers", AV_OPT_SEARCH_CHILDREN, &buffer);
  if (!buffer)
    return -1;

  icy_token = strtok((char *)buffer, "\r\n");
  while (icy_token != NULL)
    {
      ptr = strchr(icy_token, ':');
      if (!ptr || (ptr[1] == '\0'))
	{
	  icy_token = strtok(NULL, "\r\n");
	  continue;
	}

      ptr++;
      if (ptr[0] == ' ')
	ptr++;

      if (strncmp(icy_token, "icy-name", strlen("icy-name")) == 0)
	metadata->name = strdup(ptr);
      else if (strncmp(icy_token, "icy-description", strlen("icy-description")) == 0)
	metadata->description = strdup(ptr);
      else if (strncmp(icy_token, "icy-genre", strlen("icy-genre")) == 0)
	metadata->genre = strdup(ptr);

      icy_token = strtok(NULL, "\r\n");
    }
  av_free(buffer);

  return 0;
}

void
icy_metadata_free(struct icy_metadata *metadata)
{
  if (metadata->name)
    free(metadata->name);

  if (metadata->description)
    free(metadata->description);

  if (metadata->genre)
    free(metadata->genre);

  if (metadata->title)
    free(metadata->title);

  if (metadata->artist)
    free(metadata->artist);

  if (metadata->artwork_url)
    free(metadata->artwork_url);

  free(metadata);
}

#if LIBAVFORMAT_VERSION_MAJOR >= 56 || (LIBAVFORMAT_VERSION_MAJOR == 55 && LIBAVFORMAT_VERSION_MINOR >= 13)
/* Extracts ICY header and packet metadata (requires libav 10)
 *
 *   example header metadata (standard http header format):
 *     icy-name: Rock On Radio
 *   example packet metadata (track currently being played):
 *     StreamTitle='Robert Miles - Black Rubber';StreamUrl='';
 *
 * The extraction is straight from the stream and done in the player thread, so
 * it must not produce significant delay.
 */
struct icy_metadata *
icy_metadata_get(AVFormatContext *fmtctx, int packet_only)
{
  struct icy_metadata *metadata;
  int got_packet;
  int got_header;

  metadata = malloc(sizeof(struct icy_metadata));
  if (!metadata)
    return NULL;
  memset(metadata, 0, sizeof(struct icy_metadata));

  got_packet = (metadata_packet_get(metadata, fmtctx) == 0);
  got_header = (!packet_only) && (metadata_header_get(metadata, fmtctx) == 0);

  if (!got_packet && !got_header)
   {
     free(metadata);
     return NULL;
   }

/*  DPRINTF(E_DBG, L_MISC, "Found ICY: N %s, D %s, G %s, T %s, A %s, U %s, I %" PRIu32 "\n",
	metadata->name,
	metadata->description,
	metadata->genre,
	metadata->title,
	metadata->artist,
	metadata->artwork_url,
	metadata->hash
	);
*/
  return metadata;
}
#else
struct icy_metadata *
icy_metadata_get(AVFormatContext *fmtctx, int packet_only)
{
  return NULL;
}
#endif

