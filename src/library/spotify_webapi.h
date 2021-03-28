/*
 * Copyright (C) 2016 Espen Jürgensen <espenjurgensen@gmail.com>
 * Copyright (C) 2016 Christian Meffert <christian.meffert@googlemail.com>
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

#ifndef SRC_SPOTIFY_WEBAPI_H_
#define SRC_SPOTIFY_WEBAPI_H_

#include <event2/event.h>
#include <stdbool.h>

#include "http.h"


struct spotifywebapi_status_info
{
  bool token_valid;
  char user[100];
  char country[3]; // ISO 3166-1 alpha-2 country code
  char granted_scope[250];
  char required_scope[250];
};

struct spotifywebapi_access_token
{
  int expires_in;
  char *token;
};


char *
spotifywebapi_oauth_uri_get(const char *redirect_uri);
int
spotifywebapi_oauth_callback(struct evkeyvalq *param, const char *redirect_uri, char **errmsg);

void
spotifywebapi_fullrescan(void);
void
spotifywebapi_rescan(void);
void
spotifywebapi_purge(void);
void
spotifywebapi_pl_save(const char *uri);
void
spotifywebapi_pl_remove(const char *uri);
char *
spotifywebapi_artwork_url_get(const char *uri, int max_w, int max_h);

void
spotifywebapi_status_info_get(struct spotifywebapi_status_info *info);
void
spotifywebapi_access_token_get(struct spotifywebapi_access_token *info);

#endif /* SRC_SPOTIFY_WEBAPI_H_ */
