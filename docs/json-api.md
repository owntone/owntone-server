---
hide:
  - navigation
---

# OwnTone API Endpoint Reference

Available API endpoints:

* [Player](#player): control playback, volume, shuffle/repeat modes
* [Outputs / Speakers](#outputs-speakers): list available outputs and enable/disable outputs
* [Queue](#queue): list, add or modify the current queue
* [Library](#library): list playlists, artists, albums and tracks from your library or trigger library rescan
* [Search](#search): search for playlists, artists, albums and tracks
* [Server info](#server-info): get server information
* [Settings](#settings): list and change settings for the player web interface
* [Push notifications](#push-notifications): receive push notifications

JSON-Object model:

* [Queue item](#queue-item-object)
* [Playlist](#playlist-object)
* [Artist](#artist-object)
* [Album](#album-object)
* [Track](#track-object)

## Player

| Method    | Endpoint                                         | Description                          |
| --------- | ------------------------------------------------ | ------------------------------------ |
| GET       | [/api/player](#get-player-status)                | Get player status                    |
| PUT       | [/api/player/play, /api/player/pause, /api/player/stop, /api/player/toggle](#control-playback) | Start, pause or stop playback |
| PUT       | [/api/player/next, /api/player/previous](#skip-tracks) | Skip forward or backward           |
| PUT       | [/api/player/shuffle](#set-shuffle-mode)         | Set shuffle mode                     |
| PUT       | [/api/player/consume](#set-consume-mode)         | Set consume mode                     |
| PUT       | [/api/player/repeat](#set-repeat-mode)           | Set repeat mode                      |
| PUT       | [/api/player/volume](#set-volume)                | Set master volume or volume for a specific output |
| PUT       | [/api/player/seek](#seek)                        | Seek to a position in the currently playing track |



### Get player status

**Endpoint**

```http
GET /api/player
```

**Response**

| Key               | Type     | Value                                     |
| ----------------- | -------- | ----------------------------------------- |
| state             | string   | `play`, `pause` or `stop`               |
| repeat            | string   | `off`, `all` or `single`                |
| consume           | boolean  | `true` if consume mode is enabled        |
| shuffle           | boolean  | `true` if shuffle mode is enabled        |
| volume            | integer  | Master volume in percent (0 - 100)        |
| item_id           | integer  | The current playing queue item `id`      |
| item_length_ms    | integer  | Total length in milliseconds of the current queue item |
| item_progress_ms  | integer  | Progress into the current queue item in milliseconds |


**Example**

```shell
curl -X GET "http://localhost:3689/api/player"
```

```json
{
  "state": "pause",
  "repeat": "off",
  "consume": false,
  "shuffle": false,
  "volume": 50,
  "item_id": 269,
  "item_length_ms": 278093,
  "item_progress_ms": 3674
}
```


### Control playback

Start or resume, pause, stop playback.

**Endpoint**

```http
PUT /api/player/play
```

```http
PUT /api/player/pause
```

```http
PUT /api/player/stop
```

```http
PUT /api/player/toggle
```

**Response**

On success returns the HTTP `204 No Content` success status response code.

**Example**

```shell
curl -X PUT "http://localhost:3689/api/player/play"
```

```shell
curl -X PUT "http://localhost:3689/api/player/pause"
```

```shell
curl -X PUT "http://localhost:3689/api/player/stop"
```

```shell
curl -X PUT "http://localhost:3689/api/player/toggle"
```


### Skip tracks

Skip forward or backward

**Endpoint**

```http
PUT /api/player/next
```

```http
PUT /api/player/previous
```

**Response**

On success returns the HTTP `204 No Content` success status response code.

**Example**

```shell
curl -X PUT "http://localhost:3689/api/player/next"
```

```shell
curl -X PUT "http://localhost:3689/api/player/previous"
```


### Set shuffle mode

Enable or disable shuffle mode

**Endpoint**

```http
PUT /api/player/shuffle
```

**Query parameters**

| Parameter       | Value                                                       |
| --------------- | ----------------------------------------------------------- |
| state           | The new shuffle state, should be either `true` or `false` |


**Response**

On success returns the HTTP `204 No Content` success status response code.

**Example**

```shell
curl -X PUT "http://localhost:3689/api/player/shuffle?state=true"
```


### Set consume mode

Enable or disable consume mode

**Endpoint**

```http
PUT /api/player/consume
```

**Query parameters**

| Parameter       | Value                                                       |
| --------------- | ----------------------------------------------------------- |
| state           | The new consume state, should be either `true` or `false` |


**Response**

On success returns the HTTP `204 No Content` success status response code.

**Example**

```shell
curl -X PUT "http://localhost:3689/api/player/consume?state=true"
```


### Set repeat mode

Change repeat mode

**Endpoint**

```http
PUT /api/player/repeat
```

**Query parameters**

| Parameter       | Value                                                       |
| --------------- | ----------------------------------------------------------- |
| state           | The new repeat mode, should be either `off`, `all` or `single` |


**Response**

On success returns the HTTP `204 No Content` success status response code.

**Example**

```shell
curl -X PUT "http://localhost:3689/api/player/repeat?state=all"
```


### Set volume

Change master volume or volume of a specific output.

**Endpoint**

```http
PUT /api/player/volume
```

**Query parameters**

| Parameter       | Value                                                       |
| --------------- | ----------------------------------------------------------- |
| volume          | The new volume (0 - 100)                                    |
| step            | The increase or decrease volume by the given amount (-100 - 100) |
| output_id       | *(Optional)* If an output id is given, only the volume of this output will be changed. If parameter is omited, the master volume will be changed. |

Either `volume` or `step` must be present as query parameter

**Response**

On success returns the HTTP `204 No Content` success status response code.

**Example**

```shell
curl -X PUT "http://localhost:3689/api/player/volume?volume=50"
```

```shell
curl -X PUT "http://localhost:3689/api/player/volume?step=-5"
```

```shell
curl -X PUT "http://localhost:3689/api/player/volume?volume=50&output_id=0"
```


### Seek

Seek to a position in the currently playing track.

**Endpoint**

```http
PUT /api/player/seek
```

**Query parameters**

| Parameter       | Value                                                       |
| --------------- | ----------------------------------------------------------- |
| position_ms     | The new position in milliseconds to seek to                 |
| seek_ms         | A relative amount of milliseconds to seek to                 |


**Response**

On success returns the HTTP `204 No Content` success status response code.

**Example**

Seek to position:

```shell
curl -X PUT "http://localhost:3689/api/player/seek?position_ms=2000"
```

Relative seeking (skip 30 seconds backwards):

```shell
curl -X PUT "http://localhost:3689/api/player/seek?seek_ms=-30000"
```


## Outputs / Speakers

| Method    | Endpoint                                         | Description                          |
| --------- | ------------------------------------------------ | ------------------------------------ |
| GET       | [/api/outputs](#get-a-list-of-available-outputs) | Get a list of available outputs      |
| PUT       | [/api/outputs/set](#set-enabled-outputs)         | Set enabled outputs                  |
| GET       | [/api/outputs/{id}](#get-an-output)              | Get an output                        |
| PUT       | [/api/outputs/{id}](#change-an-output)           | Change an output setting             |
| PUT       | [/api/outputs/{id}/toggle](#toggle-an-output)    | Enable or disable an output, depending on the current state |



### Get a list of available outputs

**Endpoint**

```http
GET /api/outputs
```

**Response**

| Key             | Type     | Value                                     |
| --------------- | -------- | ----------------------------------------- |
| outputs         | array    | Array of `output` objects                |

**`output` object**

| Key             | Type     | Value                                     |
| --------------- | -------- | ----------------------------------------- |
| id              | string   | Output id                                 |
| name            | string   | Output name                               |
| type            | string   | Type of the output: `AirPlay`, `Chromecast`, `ALSA`, `Pulseaudio`, `fifo` |
| selected        | boolean  | `true` if output is enabled  |
| has_password    | boolean  | `true` if output is password protected |
| requires_auth   | boolean  | `true` if output requires authentication |
| needs_auth_key  | boolean  | `true` if output requires an authorization key (device verification) |
| volume          | integer  | Volume in percent (0 - 100)               |


**Example**

```shell
curl -X GET "http://localhost:3689/api/outputs"
```

```json
{
  "outputs": [
    {
      "id": "123456789012345",
      "name": "kitchen",
      "type": "AirPlay",
      "selected": true,
      "has_password": false,
      "requires_auth": false,
      "needs_auth_key": false,
      "volume": 0
    },
    {
      "id": "0",
      "name": "Computer",
      "type": "ALSA",
      "selected": true,
      "has_password": false,
      "requires_auth": false,
      "needs_auth_key": false,
      "volume": 19
    },
    {
      "id": "100",
      "name": "daapd-fifo",
      "type": "fifo",
      "selected": false,
      "has_password": false,
      "requires_auth": false,
      "needs_auth_key": false,
      "volume": 0
    }
  ]
}
```

### Set enabled outputs

Set the enabled outputs by passing an array of output ids. The server enables all outputs
with the given ids and disables the remaining outputs.

**Endpoint**

```http
PUT /api/outputs/set
```

**Body parameters**

| Parameter       | Type     | Value                |
| --------------- | -------- | -------------------- |
| outputs         | array    | Array of output ids  |

**Response**

On success returns the HTTP `204 No Content` success status response code.

**Example**

```shell
curl -X PUT "http://localhost:3689/api/outputs/set" --data "{\"outputs\":[\"198018693182577\",\"0\"]}"
```


### Get an output

Get an output

**Endpoint**

```http
GET /api/outputs/{id}
```

**Path parameters**

| Parameter       | Value                |
| --------------- | -------------------- |
| id              | Output id            |

**Response**

On success returns the HTTP `200 OK` success status response code. With the response body holding the **`output` object**.

**Example**

```shell
curl -X GET "http://localhost:3689/api/outputs/0"
```

```json
{
  "id": "0",
  "name": "Computer",
  "type": "ALSA",
  "selected": true,
  "has_password": false,
  "requires_auth": false,
  "needs_auth_key": false,
  "volume": 3
}
```

### Change an output

Enable or disable an output and change its volume.

**Endpoint**

```http
PUT /api/outputs/{id}
```

**Path parameters**

| Parameter       | Value                |
| --------------- | -------------------- |
| id              | Output id            |

**Body parameters**

| Parameter       | Type      | Value                |
| --------------- | --------- | -------------------- |
| selected        | boolean   | *(Optional)* `true` to enable and `false` to disable the output |
| volume          | integer   | *(Optional)* Volume in percent (0 - 100)  |
| pin             | string    | *(Optional)* PIN for device verification  |

**Response**

On success returns the HTTP `204 No Content` success status response code.

**Example**

```shell
curl -X PUT "http://localhost:3689/api/outputs/0" --data "{\"selected\":true, \"volume\": 50}"
```

### Toggle an output

Enable or disable an output, depending on its current state

**Endpoint**

```http
PUT /api/outputs/{id}/toggle
```

**Path parameters**

| Parameter       | Value                |
| --------------- | -------------------- |
| id              | Output id            |

**Response**

On success returns the HTTP `204 No Content` success status response code.

**Example**

```shell
curl -X PUT "http://localhost:3689/api/outputs/0/toggle"
```



## Queue

| Method    | Endpoint                                                    | Description                          |
| --------- | ----------------------------------------------------------- | ------------------------------------ |
| GET       | [/api/queue](#list-queue-items)                             | Get a list of queue items            |
| PUT       | [/api/queue/clear](#clearing-the-queue)                     | Remove all items from the queue      |
| POST      | [/api/queue/items/add](#adding-items-to-the-queue)          | Add items to the queue               |
| PUT       | [/api/queue/items/{id}\|now_playing](#updating-a-queue-item)| Updating a queue item in the queue   |
| DELETE    | [/api/queue/items/{id}](#removing-a-queue-item)             | Remove a queue item from the queue   |



### List queue items

Lists the items in the current queue

**Endpoint**

```http
GET /api/queue
```

**Query parameters**

| Parameter       | Value                                                       |
| --------------- | ----------------------------------------------------------- |
| id              | *(Optional)* If a queue item id is given, only the item with the id will be returend. Use id=now_playing to get the currently playing item. |
| start           | *(Optional)* If a `start`and an `end` position is given, only the items from `start` (included) to `end` (excluded) will be returned. If only a `start` position is given, only the item at this position will be returned. |
| end             | *(Optional)* See `start` parameter |

**Response**

| Key             | Type     | Value                                     |
| --------------- | -------- | ----------------------------------------- |
| version         | integer  | Version number of the current queue       |
| count           | integer  | Number of items in the current queue      |
| items           | array    | Array of [`queue item`](#queue-item-object) objects |

**Example**

```shell
curl -X GET "http://localhost:3689/api/queue"
```

```json
{
  "version": 833,
  "count": 20,
  "items": [
    {
      "id": 12122,
      "position": 0,
      "track_id": 10749,
      "title": "Angels",
      "artist": "The xx",
      "artist_sort": "xx, The",
      "album": "Coexist",
      "album_sort": "Coexist",
      "albumartist": "The xx",
      "albumartist_sort": "xx, The",
      "genre": "Indie Rock",
      "year": 2012,
      "track_number": 1,
      "disc_number": 1,
      "length_ms": 171735,
      "media_kind": "music",
      "data_kind": "file",
      "path": "/music/srv/The xx/Coexist/01 Angels.mp3",
      "uri": "library:track:10749"
    },
    ...
  ]
}
```


### Clearing the queue

Remove all items form the current queue

**Endpoint**

```http
PUT /api/queue/clear
```

**Response**

On success returns the HTTP `204 No Content` success status response code.

**Example**

```shell
curl -X PUT "http://localhost:3689/api/queue/clear"
```


### Adding items to the queue

Add tracks, playlists artists or albums to the current queue

**Endpoint**

```http
POST /api/queue/items/add
```

**Query parameters**

| Parameter       | Value                                                       |
| --------------- | ----------------------------------------------------------- |
| uris            | Comma seperated list of resource identifiers (`track`, `playlist`, `artist` or `album` object `uri`)           |
| expression      | A smart playlist query expression identifying the tracks that will be added to the queue.                          |
| position        | *(Optional)* If a position is given, new items are inserted starting from this position into the queue.            |
| playback        | *(Optional)* If the `playback` parameter is set to `start`, playback will be started after adding the new items. |
| playback_from_position | *(Optional)* If the `playback` parameter is set to `start`, playback will be started with the queue item at the position given in `playback_from_position`. |
| clear           | *(Optional)* If the `clear` parameter is set to `true`, the queue will be cleared before adding the new items.    |
| shuffle         | *(Optional)* If the `shuffle` parameter is set to `true`, the shuffle mode is activated. If it is set to something else, the shuffle mode is deactivated. To leave the shuffle mode untouched the parameter should be ommited.    |
| limit           | *(Optional)* Maximum number of tracks to add |

Either the `uris` or the `expression` parameter must be set. If both are set the `uris` parameter takes presedence and the `expression` parameter will be ignored.

**Response**

On success returns the HTTP `200 OK` success status response code.

| Key             | Type     | Value                                     |
| --------------- | -------- | ----------------------------------------- |
| count           | integer  | number of tracks added to the queue       |


**Example**

Add new items by uri:

```shell
curl -X POST "http://localhost:3689/api/queue/items/add?uris=library:playlist:68,library:artist:2932599850102967727"
```

```json
{
  "count": 42
}
```

Add new items by query language:

```shell
curl -X POST "http://localhost:3689/api/queue/items/add?expression=media_kind+is+music"
```

```json
{
  "count": 42
}
```

Clear current queue, add 10 new random tracks of `genre` _Pop_ and start playback
```
curl -X POST "http://localhost:3689/api/queue/items/add?limit=10&clear=true&playback=start&expression=genre+is+%22Pop%22+order+by+random+desc"
```

```json
{
  "count": 10
}
```

### Updating a queue item

Update or move a queue item in the current queue

**Endpoint**

```http
PUT /api/queue/items/{id}
```
or
```http
PUT /api/queue/items/now_playing
```

**Path parameters**

| Parameter       | Value                |
| --------------- | -------------------- |
| id              | Queue item id        |

(or use now_playing to update the currenly playing track)

**Query parameters**

| Parameter       | Value                                                       |
| --------------- | ----------------------------------------------------------- |
| new_position    | The new position for the queue item in the current queue.   |
| title           | New track title                                             |
| album           | New album title                                             |
| artist          | New artist                                                  |
| album_artist    | New album artist                                            |
| composer        | New composer                                                |
| genre           | New genre                                                   |
| artwork_url     | New URL to track artwork                                    |

**Response**

On success returns the HTTP `204 No Content` success status response code.

**Example**

```shell
curl -X PUT "http://localhost:3689/api/queue/items/3?new_position=0"
```

```shell
curl -X PUT "http://localhost:3689/api/queue/items/3?title=Awesome%20title&artwork_url=http%3A%2F%2Fgyfgafguf.dk%2Fimages%2Fpige3.jpg"
```

```shell
curl -X PUT "http://localhost:3689/api/queue/items/now_playing?title=Awesome%20title&artwork_url=http%3A%2F%2Fgyfgafguf.dk%2Fimages%2Fpige3.jpg"
```

### Removing a queue item

Remove a queue item from the current queue

**Endpoint**

```http
DELETE /api/queue/items/{id}
```

**Path parameters**

| Parameter       | Value                |
| --------------- | -------------------- |
| id              | Queue item id        |

**Response**

On success returns the HTTP `204 No Content` success status response code.

**Example**

```shell
curl -X PUT "http://localhost:3689/api/queue/items/2"
```



## Library

| Method    | Endpoint                                                    | Description                          |
| --------- | ----------------------------------------------------------- | ------------------------------------ |
| GET       | [/api/library](#library-information)                        | Get library information              |
| GET       | [/api/library/playlists](#list-playlists)                   | Get a list of playlists              |
| GET       | [/api/library/playlists/{id}](#get-a-playlist)              | Get a playlist                       |
| PUT       | [/api/library/playlists/{id}](#update-a-playlist)           | Update a playlist attribute          |
| DELETE    | [/api/library/playlists/{id}](#delete-a-playlist)           | Delete a playlist                    |
| GET       | [/api/library/playlists/{id}/tracks](#list-playlist-tracks) | Get list of tracks for a playlist    |
| PUT       | [/api/library/playlists/{id}/tracks](#update-playlist-tracks) | Update play count of tracks for a playlist    |
| GET       | [/api/library/playlists/{id}/playlists](#list-playlists-in-a-playlist-folder) | Get list of playlists for a playlist folder   |
| GET       | [/api/library/artists](#list-artists)                       | Get a list of artists                |
| GET       | [/api/library/artists/{id}](#get-an-artist)                 | Get an artist                        |
| GET       | [/api/library/artists/{id}/albums](#list-artist-albums)     | Get list of albums for an artist     |
| GET       | [/api/library/albums](#list-albums)                         | Get a list of albums                 |
| GET       | [/api/library/albums/{id}](#get-an-album)                   | Get an album                         |
| GET       | [/api/library/albums/{id}/tracks](#list-album-tracks)       | Get list of tracks for an album      |
| GET       | [/api/library/tracks/{id}](#get-a-track)                    | Get a track                          |
| GET       | [/api/library/tracks/{id}/playlists](#list-playlists-for-a-track) | Get list of playlists for a track |
| PUT       | [/api/library/tracks](#update-track-properties)             | Update multiple track properties     |
| PUT       | [/api/library/tracks/{id}](#update-track-properties)        | Update single track properties       |
| GET       | [/api/library/genres](#list-genres)                         | Get list of genres                   |
| GET       | [/api/library/count](#get-count-of-tracks-artists-and-albums) | Get count of tracks, artists and albums |
| GET       | [/api/library/files](#list-local-directories)               | Get list of directories in the local library    |
| POST      | [/api/library/add](#add-an-item-to-the-library)             | Add an item to the library           |
| PUT       | [/api/update](#trigger-rescan)                              | Trigger a library rescan             |
| PUT       | [/api/rescan](#trigger-metadata-rescan)                     | Trigger a library metadata rescan    |
| PUT       | [/api/library/backup](#backup-db)                           | Request library backup db            |



### Library information

List some library stats

**Endpoint**

```http
GET /api/library
```

**Response**

| Key             | Type     | Value                                     |
| --------------- | -------- | ----------------------------------------- |
| songs           | integer  | Array of [`playlist`](#playlist-object) objects           |
| db_playtime     | integer  | Total playtime of all songs in the library  |
| artists         | integer  | Number of album artists in the library    |
| albums          | integer  | Number of albums in the library           |
| started_at      | string   | Server startup time (timestamp in `ISO 8601` format)     |
| updated_at      | string   | Last library update (timestamp in `ISO 8601` format)     |
| updating        | boolean  | `true` if library rescan is in progress  |


**Example**

```shell
curl -X GET "http://localhost:3689/api/library"
```

```json
{
  "songs": 217,
  "db_playtime": 66811,
  "artists": 9,
  "albums": 19,
  "started_at": "2018-11-19T19:06:08Z",
  "updated_at": "2018-11-19T19:06:16Z",
  "updating": false
}
```


### List playlists

Lists all playlists in your library (does not return playlist folders)

**Endpoint**

```http
GET /api/library/playlists
```

**Query parameters**

| Parameter       | Value                                                       |
| --------------- | ----------------------------------------------------------- |
| offset          | *(Optional)* Offset of the first playlist to return         |
| limit           | *(Optional)* Maximum number of playlists to return          |

**Response**

| Key             | Type     | Value                                     |
| --------------- | -------- | ----------------------------------------- |
| items           | array    | Array of [`playlist`](#playlist-object) objects              |
| total           | integer  | Total number of playlists in the library  |
| offset          | integer  | Requested offset of the first playlist    |
| limit           | integer  | Requested maximum number of playlists     |


**Example**

```shell
curl -X GET "http://localhost:3689/api/library/playlists"
```

```json
{
  "items": [
    {
      "id": 1,
      "name": "radio",
      "path": "/music/srv/radio.m3u",
      "smart_playlist": false,
      "uri": "library:playlist:1"
    },
    ...
  ],
  "total": 20,
  "offset": 0,
  "limit": -1
}
```


### Get a playlist

Get a specific playlists in your library

**Endpoint**

```http
GET /api/library/playlists/{id}
```

**Path parameters**

| Parameter       | Value                |
| --------------- | -------------------- |
| id              | Playlist id          |

**Response**

On success returns the HTTP `200 OK` success status response code. With the response body holding the **[`playlist`](#playlist-object) object**.


**Example**

```shell
curl -X GET "http://localhost:3689/api/library/playlists/1"
```

```json
{
  "id": 1,
  "name": "radio",
  "path": "/music/srv/radio.m3u",
  "smart_playlist": false,
  "uri": "library:playlist:1"
}
```


### Update a playlist

Update attributes of a specific playlists in your library

**Endpoint**

```http
PUT /api/library/playlists/{id}
```

**Path parameters**

| Parameter       | Value                |
| --------------- | -------------------- |
| id              | Playlist id          |

**Query parameters**

| Parameter       | Value                                                       |
| --------------- | ----------------------------------------------------------- |
| query_limit     | For RSS feeds, this sets how many podcasts to retrieve      |


**Example**

```shell
curl -X PUT "http://localhost:3689/api/library/playlists/25?query_limit=20"
```


### Delete a playlist

Delete a playlist, e.g. a RSS feed

**Endpoint**

```http
DELETE /api/library/playlists/{id}
```

**Path parameters**

| Parameter       | Value                |
| --------------- | -------------------- |
| id              | Playlist id          |

**Example**

```shell
curl -X DELETE "http://localhost:3689/api/library/playlists/25"
```


### List playlist tracks

Lists the tracks in a playlists

**Endpoint**

```http
GET /api/library/playlists/{id}/tracks
```

**Path parameters**

| Parameter       | Value                |
| --------------- | -------------------- |
| id              | Playlist id          |

**Query parameters**

| Parameter       | Value                                                       |
| --------------- | ----------------------------------------------------------- |
| offset          | *(Optional)* Offset of the first track to return            |
| limit           | *(Optional)* Maximum number of tracks to return             |

**Response**

| Key             | Type     | Value                                     |
| --------------- | -------- | ----------------------------------------- |
| items           | array    | Array of [`track`](#track-object) objects |
| total           | integer  | Total number of tracks in the playlist    |
| offset          | integer  | Requested offset of the first track       |
| limit           | integer  | Requested maximum number of tracks        |


**Example**

```shell
curl -X GET "http://localhost:3689/api/library/playlists/1/tracks"
```

```json
{
  "items": [
    {
      "id": 10766,
      "title": "Solange wir tanzen",
      "artist": "Heinrich",
      "artist_sort": "Heinrich",
      "album": "Solange wir tanzen",
      "album_sort": "Solange wir tanzen",
      "albumartist": "Heinrich",
      "albumartist_sort": "Heinrich",
      "genre": "Electronica",
      "year": 2014,
      "track_number": 1,
      "disc_number": 1,
      "length_ms": 223085,
      "play_count": 2,
      "skip_count": 1,
      "time_played": "2018-02-23T10:31:20Z",
      "media_kind": "music",
      "data_kind": "file",
      "path": "/music/srv/Heinrich/Solange wir tanzen/01 Solange wir tanzen.mp3",
      "uri": "library:track:10766"
    },
    ...
  ],
  "total": 20,
  "offset": 0,
  "limit": -1
}
```

### Update playlist tracks

Updates the play count for tracks in a playlists

**Endpoint**

```http
PUT /api/library/playlists/{id}/tracks
```

**Path parameters**

| Parameter       | Value                |
| --------------- | -------------------- |
| id              | Playlist id          |

**Query parameters**

| Parameter       | Value                                                       |
| --------------- | ----------------------------------------------------------- |
| play_count      | Either `increment`, `played` or `reset`. `increment` will increment `play_count` and update `time_played`, `played` will be like `increment` but only where `play_count` is 0, `reset` will set `play_count` and `skip_count` to zero and delete `time_played` and `time_skipped` |


**Example**

```shell
curl -X PUT "http://localhost:3689/api/library/playlists/1/tracks?play_count=played"
```

### List playlists in a playlist folder

Lists the playlists in a playlist folder

**Note**: The root playlist folder has `id` 0.

**Endpoint**

```http
GET /api/library/playlists/{id}/playlists
```

**Path parameters**

| Parameter       | Value                |
| --------------- | -------------------- |
| id              | Playlist id          |

**Query parameters**

| Parameter       | Value                                                       |
| --------------- | ----------------------------------------------------------- |
| offset          | *(Optional)* Offset of the first playlist to return         |
| limit           | *(Optional)* Maximum number of playlist to return           |

**Response**

| Key             | Type     | Value                                            |
| --------------- | -------- | ------------------------------------------------ |
| items           | array    | Array of [`playlist`](#playlist-object) objects  |
| total           | integer  | Total number of playlists in the playlist folder |
| offset          | integer  | Requested offset of the first playlist           |
| limit           | integer  | Requested maximum number of playlist             |


**Example**

```shell
curl -X GET "http://localhost:3689/api/library/playlists/0/tracks"
```

```json
{
  "items": [
    {
      "id": 11,
      "name": "Spotify",
      "path": "spotify:playlistfolder",
      "parent_id": "0",
      "smart_playlist": false,
      "folder": true,
      "uri": "library:playlist:11"
    },
    {
      "id": 8,
      "name": "bytefm",
      "path": "/srv/music/Playlists/bytefm.m3u",
      "parent_id": "0",
      "smart_playlist": false,
      "folder": false,
      "uri": "library:playlist:8"
    }
  ],
  "total": 2,
  "offset": 0,
  "limit": -1
}
```


### List artists

Lists the artists in your library

**Endpoint**

```http
GET /api/library/artists
```

**Query parameters**

| Parameter       | Value                                                       |
| --------------- | ----------------------------------------------------------- |
| offset          | *(Optional)* Offset of the first artist to return           |
| limit           | *(Optional)* Maximum number of artists to return            |

**Response**

| Key             | Type     | Value                                       |
| --------------- | -------- | ------------------------------------------- |
| items           | array    | Array of [`artist`](#artist-object) objects |
| total           | integer  | Total number of artists in the library      |
| offset          | integer  | Requested offset of the first artist        |
| limit           | integer  | Requested maximum number of artists         |


**Example**

```shell
curl -X GET "http://localhost:3689/api/library/artists"
```

```json
{
  "items": [
    {
      "id": "3815427709949443149",
      "name": "ABAY",
      "name_sort": "ABAY",
      "album_count": 1,
      "track_count": 10,
      "length_ms": 2951554,
      "uri": "library:artist:3815427709949443149"
    },
    ...
  ],
  "total": 20,
  "offset": 0,
  "limit": -1
}
```


### Get an artist

Get a specific artist in your library

**Endpoint**

```http
GET /api/library/artists/{id}
```

**Path parameters**

| Parameter       | Value                |
| --------------- | -------------------- |
| id              | Artist id            |

**Response**

On success returns the HTTP `200 OK` success status response code. With the response body holding the **[`artist`](#artist-object) object**.


**Example**

```shell
curl -X GET "http://localhost:3689/api/library/artists/3815427709949443149"
```

```json
{
  "id": "3815427709949443149",
  "name": "ABAY",
  "name_sort": "ABAY",
  "album_count": 1,
  "track_count": 10,
  "length_ms": 2951554,
  "uri": "library:artist:3815427709949443149"
}
```


### List artist albums

Lists the albums of an artist

**Endpoint**

```http
GET /api/library/artists/{id}/albums
```

**Path parameters**

| Parameter       | Value                |
| --------------- | -------------------- |
| id              | Artist id            |

**Query parameters**

| Parameter       | Value                                                       |
| --------------- | ----------------------------------------------------------- |
| offset          | *(Optional)* Offset of the first album to return            |
| limit           | *(Optional)* Maximum number of albums to return             |

**Response**

| Key             | Type     | Value                                     |
| --------------- | -------- | ----------------------------------------- |
| items           | array    | Array of [`album`](#album-object) objects |
| total           | integer  | Total number of albums of this artist     |
| offset          | integer  | Requested offset of the first album       |
| limit           | integer  | Requested maximum number of albums        |


**Example**

```shell
curl -X GET "http://localhost:3689/api/library/artists/32561671101664759/albums"
```

```json
{
  "items": [
    {
      "id": "8009851123233197743",
      "name": "Add Violence",
      "name_sort": "Add Violence",
      "artist": "Nine Inch Nails",
      "artist_id": "32561671101664759",
      "track_count": 5,
      "length_ms": 1634961,
      "uri": "library:album:8009851123233197743"
    },
    ...
  ],
  "total": 20,
  "offset": 0,
  "limit": -1
}
```


### List albums

Lists the albums in your library

**Endpoint**

```http
GET /api/library/albums
```

**Query parameters**

| Parameter       | Value                                                       |
| --------------- | ----------------------------------------------------------- |
| offset          | *(Optional)* Offset of the first album to return            |
| limit           | *(Optional)* Maximum number of albums to return             |

**Response**

| Key             | Type     | Value                                     |
| --------------- | -------- | ----------------------------------------- |
| items           | array    | Array of [`album`](#album-object) objects |
| total           | integer  | Total number of albums in the library     |
| offset          | integer  | Requested offset of the first albums      |
| limit           | integer  | Requested maximum number of albums        |


**Example**

```shell
curl -X GET "http://localhost:3689/api/library/albums"
```

```json
{
  "items": [
    {
      "id": "8009851123233197743",
      "name": "Add Violence",
      "name_sort": "Add Violence",
      "artist": "Nine Inch Nails",
      "artist_id": "32561671101664759",
      "track_count": 5,
      "length_ms": 1634961,
      "uri": "library:album:8009851123233197743"
    },
    ...
  ],
  "total": 20,
  "offset": 0,
  "limit": -1
}
```


### Get an album

Get a specific album in your library

**Endpoint**

```http
GET /api/library/albums/{id}
```

**Path parameters**

| Parameter       | Value                |
| --------------- | -------------------- |
| id              | Album id             |

**Response**

On success returns the HTTP `200 OK` success status response code. With the response body holding the **[`album`](#album-object) object**.


**Example**

```shell
curl -X GET "http://localhost:3689/api/library/albums/8009851123233197743"
```

```json
{
  "id": "8009851123233197743",
  "name": "Add Violence",
  "name_sort": "Add Violence",
  "artist": "Nine Inch Nails",
  "artist_id": "32561671101664759",
  "track_count": 5,
  "length_ms": 1634961,
  "uri": "library:album:8009851123233197743"
}
```


### List album tracks

Lists the tracks in an album

**Endpoint**

```http
GET /api/library/albums/{id}/tracks
```

**Path parameters**

| Parameter       | Value                |
| --------------- | -------------------- |
| id              | Album id             |

**Query parameters**

| Parameter       | Value                                                       |
| --------------- | ----------------------------------------------------------- |
| offset          | *(Optional)* Offset of the first track to return            |
| limit           | *(Optional)* Maximum number of tracks to return             |

**Response**

| Key             | Type     | Value                                     |
| --------------- | -------- | ----------------------------------------- |
| items           | array    | Array of [`track`](#track-object) objects |
| total           | integer  | Total number of tracks                    |
| offset          | integer  | Requested offset of the first track       |
| limit           | integer  | Requested maximum number of tracks        |


**Example**

```shell
curl -X GET "http://localhost:3689/api/library/albums/1/tracks"
```

```json
{
  "items": [
    {
      "id": 10766,
      "title": "Solange wir tanzen",
      "artist": "Heinrich",
      "artist_sort": "Heinrich",
      "album": "Solange wir tanzen",
      "album_sort": "Solange wir tanzen",
      "albumartist": "Heinrich",
      "albumartist_sort": "Heinrich",
      "genre": "Electronica",
      "year": 2014,
      "track_number": 1,
      "disc_number": 1,
      "length_ms": 223085,
      "play_count": 2,
      "last_time_played": "2018-02-23T10:31:20Z",
      "media_kind": "music",
      "data_kind": "file",
      "path": "/music/srv/Heinrich/Solange wir tanzen/01 Solange wir tanzen.mp3",
      "uri": "library:track:10766"
    },
    ...
  ],
  "total": 20,
  "offset": 0,
  "limit": -1
}
```


### Get a track

Get a specific track in your library

**Endpoint**

```http
GET /api/library/tracks/{id}
```

**Path parameters**

| Parameter       | Value                |
| --------------- | -------------------- |
| id              | Track id             |

**Response**

On success returns the HTTP `200 OK` success status response code. With the response body holding the **[`track`](#track-object) object**.


**Example**

```shell
curl -X GET "http://localhost:3689/api/library/track/1"
```

```json
{
  "id": 1,
  "title": "Pardon Me",
  "title_sort": "Pardon Me",
  "artist": "Incubus",
  "artist_sort": "Incubus",
  "album": "Make Yourself",
  "album_sort": "Make Yourself",
  "album_id": "6683985628074308431",
  "album_artist": "Incubus",
  "album_artist_sort": "Incubus",
  "album_artist_id": "4833612337650426236",
  "composer": "Alex Katunich/Brandon Boyd/Chris Kilmore/Jose Antonio Pasillas II/Mike Einziger",
  "genre": "Alternative Rock",
  "year": 2001,
  "track_number": 12,
  "disc_number": 1,
  "length_ms": 223170,
  "rating": 0,
  "usermark": 0,
  "play_count": 0,
  "skip_count": 0,
  "time_added": "2019-01-20T11:58:29Z",
  "date_released": "2001-05-27",
  "seek_ms": 0,
  "media_kind": "music",
  "data_kind": "file",
  "path": "/music/srv/Incubus/Make Yourself/12 Pardon Me.mp3",
  "uri": "library:track:1",
  "artwork_url": "/artwork/item/1"
}
```


### List playlists for a track

Get the list of playlists that contain a track (does not return smart playlists)

**Endpoint**

```http
GET /api/library/tracks/{id}/playlists
```

**Path parameters**

| Parameter       | Value                |
| --------------- | -------------------- |
| id              | Track id             |

**Query parameters**

| Parameter       | Value                                                       |
| --------------- | ----------------------------------------------------------- |
| offset          | *(Optional)* Offset of the first playlist to return         |
| limit           | *(Optional)* Maximum number of playlist to return           |

**Response**

| Key             | Type     | Value                                     |
| --------------- | -------- | ----------------------------------------- |
| items           | array    | Array of [`playlist`](#playlist-object) objects              |
| total           | integer  | Total number of playlists                 |
| offset          | integer  | Requested offset of the first playlist    |
| limit           | integer  | Requested maximum number of playlists     |

**Example**

```shell
curl -X GET "http://localhost:3689/api/library/tracks/27/playlists"
```

```json
{
  "items": [
    {
      "id": 1,
      "name": "playlist",
      "path": "/music/srv/playlist.m3u",
      "smart_playlist": false,
      "uri": "library:playlist:1"
    },
    ...
  ],
  "total": 2,
  "offset": 0,
  "limit": -1
}
```


### Update track properties

Change properties of one or more tracks (supported properties are "rating", "play_count" and "usermark")

**Endpoint**

```http
PUT /api/library/tracks
```

**Body parameters**

| Parameter       | Type     | Value                   |
| --------------- | -------- | ----------------------- |
| tracks          | array    | Array of track objects  |


**Response**

On success returns the HTTP `204 No Content` success status response code.

**Example**

```shell
curl -X PUT -d '{ "tracks": [ { "id": 1, "rating": 100, "usermark": 4 }, { "id": 2, "usermark": 3 } ] }' "http://localhost:3689/api/library/tracks"
```

**Endpoint**

```http
PUT /api/library/tracks/{id}
```

**Path parameters**

| Parameter       | Value                |
| --------------- | -------------------- |
| id              | Track id             |

**Query parameters**

| Parameter       | Value                                                       |
| --------------- | ----------------------------------------------------------- |
| rating          | The new rating (0 - 100)                                    |
| play_count      | Either `increment` or `reset`. `increment` will increment `play_count` and update `time_played`, `reset` will set `play_count` and `skip_count` to zero and delete `time_played` and `time_skipped` |
| usermark        | The new usermark (>= 0)                                     |


**Response**

On success returns the HTTP `204 No Content` success status response code.

**Example**

```shell
curl -X PUT "http://localhost:3689/api/library/tracks/1?rating=100"
```

```shell
curl -X PUT "http://localhost:3689/api/library/tracks/1?play_count=increment"
```


### List genres

Get list of genres

**Endpoint**

```http
GET /api/library/genres
```
**Response**

| Key             | Type     | Value                                     |
| --------------- | -------- | ----------------------------------------- |
| items           | array    | Array of [`browse-info`](#browse-info-object) objects |
| total           | integer  | Total number of genres in the library     |
| offset          | integer  | Requested offset of the first genre       |
| limit           | integer  | Requested maximum number of genres        |


**Example**

```shell
curl -X GET "http://localhost:3689/api/library/genres"
```

```json
{
  "items": [
    {
      "name": "Classical"
    },
    {
      "name": "Drum & Bass"
    },
    {
      "name": "Pop"
    },
    {
      "name": "Rock/Pop"
    },
    {
      "name": "'90s Alternative"
    }
  ],
  "total": 5,
  "offset": 0,
  "limit": -1
}

```

### List albums for genre

Lists the albums in a genre

**Endpoint**

```http
GET api/search?type=albums&expression=genre+is+\"{genre name}\""
```

**Query parameters**

| Parameter       | Value                                                       |
| --------------- | ----------------------------------------------------------- |
| genre           | genre name (uri encoded and html esc seq for chars: '/&)    |
| offset          | *(Optional)* Offset of the first album to return            |
| limit           | *(Optional)* Maximum number of albums to return             |

**Response**

| Key             | Type     | Value                                     |
| --------------- | -------- | ----------------------------------------- |
| items           | array    | Array of [`album`](#album-object) objects |
| total           | integer  | Total number of albums in the library     |
| offset          | integer  | Requested offset of the first albums      |
| limit           | integer  | Requested maximum number of albums        |


**Example**

```shell
curl -X GET "http://localhost:3689/api/search?type=albums&expression=genre+is+\"Pop\""
curl -X GET "http://localhost:3689/api/search?type=albums&expression=genre+is+\"Rock%2FPop\""            # Rock/Pop
curl -X GET "http://localhost:3689/api/search?type=albums&expression=genre+is+\"Drum%20%26%20Bass\""     # Drum & Bass
curl -X GET "http://localhost:3689/api/search?type=albums&expression=genre+is+\"%2790s%20Alternative\""  # '90 Alternative
```

```json
{
  "albums": {
    "items": [
      {
        "id": "320189328729146437",
        "name": "Best Ever",
        "name_sort": "Best Ever",
        "artist": "ABC",
        "artist_id": "8760559201889050080",
        "track_count": 1,
        "length_ms": 3631,
        "uri": "library:album:320189328729146437"
      },
      {
        "id": "7964595866631625723",
        "name": "Greatest Hits",
        "name_sort": "Greatest Hits",
        "artist": "Marvin Gaye",
        "artist_id": "5261930703203735930",
        "track_count": 2,
        "length_ms": 7262,
        "uri": "library:album:7964595866631625723"
      },
      {
        "id": "3844610748145176456",
        "name": "The Very Best of Etta",
        "name_sort": "Very Best of Etta",
        "artist": "Etta James",
        "artist_id": "2627182178555864595",
        "track_count": 1,
        "length_ms": 177926,
        "uri": "library:album:3844610748145176456"
      }
    ],
    "total": 3,
    "offset": 0,
    "limit": -1
  }
}
```

### Get count of tracks, artists and albums

Get information about the number of tracks, artists and albums and the total playtime

**Endpoint**

```http
GET /api/library/count
```

**Query parameters**

| Parameter       | Value                                                       |
| --------------- | ----------------------------------------------------------- |
| expression      | *(Optional)* The smart playlist query expression, if this parameter is omitted returns the information for the whole library |

**Response**

| Key             | Type     | Value                                     |
| --------------- | -------- | ----------------------------------------- |
| tracks          | integer  | Number of tracks matching the expression  |
| artists         | integer  | Number of artists matching the expression |
| albums          | integer  | Number of albums matching the expression  |
| db_playtime     | integer  | Total playtime in milliseconds of all tracks matching the expression |


**Example**

```shell
curl -X GET "http://localhost:3689/api/library/count?expression=data_kind+is+file"
```

```json
{
  "tracks": 6811,
  "artists": 355,
  "albums": 646,
  "db_playtime": 1590767
}
```


### List local directories

List the local directories and the directory contents (tracks and playlists)


**Endpoint**

```http
GET /api/library/files
```

**Query parameters**

| Parameter       | Value                                                       |
| --------------- | ----------------------------------------------------------- |
| directory       | *(Optional)* A path to a directory in your local library.   |

**Response**

| Key             | Type     | Value                                     |
| --------------- | -------- | ----------------------------------------- |
| directories     | array    | Array of [`directory`](#directory-object) objects containing the sub directories |
| tracks          | object   | [`paging`](#paging-object) object containing [`track`](#track-object) objects that matches the `directory`   |
| playlists       | object   | [`paging`](#paging-object) object containing [`playlist`](#playlist-object) objects that matches the `directory`   |


**Example**

```shell
curl -X GET "http://localhost:3689/api/library/files?directory=/music/srv"
```

```json
{
  "directories": [
    {
      "path": "/music/srv/Audiobooks"
    },
    {
      "path": "/music/srv/Music"
    },
    {
      "path": "/music/srv/Playlists"
    },
    {
      "path": "/music/srv/Podcasts"
    }
  ],
  "tracks": {
    "items": [
      {
        "id": 1,
        "title": "input.pipe",
        "artist": "Unknown artist",
        "artist_sort": "Unknown artist",
        "album": "Unknown album",
        "album_sort": "Unknown album",
        "album_id": "4201163758598356043",
        "album_artist": "Unknown artist",
        "album_artist_sort": "Unknown artist",
        "album_artist_id": "4187901437947843388",
        "genre": "Unknown genre",
        "year": 0,
        "track_number": 0,
        "disc_number": 0,
        "length_ms": 0,
        "play_count": 0,
        "skip_count": 0,
        "time_added": "2018-11-24T08:41:35Z",
        "seek_ms": 0,
        "media_kind": "music",
        "data_kind": "pipe",
        "path": "/music/srv/input.pipe",
        "uri": "library:track:1",
        "artwork_url": "/artwork/item/1"
      }
    ],
    "total": 1,
    "offset": 0,
    "limit": -1
  },
  "playlists": {
    "items": [
      {
        "id": 8,
        "name": "radio",
        "path": "/music/srv/radio.m3u",
        "smart_playlist": true,
        "uri": "library:playlist:8"
      }
    ],
    "total": 1,
    "offset": 0,
    "limit": -1
  }
}
```


### Trigger rescan

Trigger a library rescan

**Endpoint**

```http
PUT /api/update
```

**Response**

On success returns the HTTP `204 No Content` success status response code.

**Example**

```shell
curl -X PUT "http://localhost:3689/api/update"
```

```json
{
  "songs": 217,
  "db_playtime": 66811,
  "artists": 9,
  "albums": 19,
  "started_at": "2018-11-19T19:06:08Z",
  "updated_at": "2018-11-19T19:06:16Z",
  "updating": false
}
```

### Trigger metadata rescan

Trigger a library metadata rescan even if files have not been updated.  Maintenence method.

**Endpoint**

```http
PUT /api/rescan
```

**Response**

On success returns the HTTP `204 No Content` success status response code.

**Example**

```shell
curl -X PUT "http://localhost:3689/api/rescan"
```

### Backup DB

Request a library backup - configuration must be enabled and point to a valid writable path. Maintenance method.

**Endpoint**

```http
PUT /api/library/backup
```

**Response**

On success returns the HTTP `200 OK` success status response code.
If backups are not enabled returns HTTP `503 Service Unavailable` response code.
Otherwise a HTTP `500 Internal Server Error` response is returned.

**Example**

```shell
curl -X PUT "http://localhost:3689/api/library/backup"
```

## Search

| Method    | Endpoint                                                    | Description                          |
| --------- | ----------------------------------------------------------- | ------------------------------------ |
| GET       | [/api/search](#search-by-search-term)                       | Search for playlists, artists, albums, tracks,genres by a simple search term |
| GET       | [/api/search](#search-by-query-language)                    | Search by complex query expression   |



### Search by search term

Search for playlists, artists, albums, tracks, genres that include the given query in their title (case insensitive matching).

**Endpoint**

```http
GET /api/search
```

**Query parameters**

| Parameter       | Value                                                       |
| --------------- | ----------------------------------------------------------- |
| query           | The search keyword                                          |
| type            | Comma separated list of the result types (`playlist`, `artist`, `album`, `track`, `genre`) |
| media_kind      | *(Optional)* Filter results by media kind (`music`, `movie`, `podcast`, `audiobook`, `musicvideo`, `tvshow`). Filter only applies to artist, album and track result types. |
| offset          | *(Optional)* Offset of the first item to return for each type |
| limit           | *(Optional)* Maximum number of items to return for each type  |

**Response**

| Key             | Type     | Value                                     |
| --------------- | -------- | ----------------------------------------- |
| tracks          | object   | [`paging`](#paging-object) object containing [`track`](#track-object) objects that matches the `query` |
| artists         | object   | [`paging`](#paging-object) object containing [`artist`](#artist-object) objects that matches the `query` |
| albums          | object   | [`paging`](#paging-object) object containing [`album`](#album-object) objects that matches the `query` |
| playlists       | object   | [`paging`](#paging-object) object containing [`playlist`](#playlist-object) objects that matches the `query` |


**Example**

Search for all tracks, artists, albums and playlists that contain "the" in their title and return the first two results for each type:

```shell
curl -X GET "http://localhost:3689/api/search?type=tracks,artists,albums,playlists&query=the&offset=0&limit=2"
```

```json
{
  "tracks": {
    "items": [
      {
        "id": 35,
        "title": "Another Love",
        "artist": "Tom Odell",
        "artist_sort": "Tom Odell",
        "album": "Es is was es is",
        "album_sort": "Es is was es is",
        "album_id": "6494853621007413058",
        "album_artist": "Various artists",
        "album_artist_sort": "Various artists",
        "album_artist_id": "8395563705718003786",
        "genre": "Singer/Songwriter",
        "year": 2013,
        "track_number": 7,
        "disc_number": 1,
        "length_ms": 251030,
        "play_count": 0,
        "media_kind": "music",
        "data_kind": "file",
        "path": "/music/srv/Compilations/Es is was es is/07 Another Love.m4a",
        "uri": "library:track:35"
      },
      {
        "id": 215,
        "title": "Away From the Sun",
        "artist": "3 Doors Down",
        "artist_sort": "3 Doors Down",
        "album": "Away From the Sun",
        "album_sort": "Away From the Sun",
        "album_id": "8264078270267374619",
        "album_artist": "3 Doors Down",
        "album_artist_sort": "3 Doors Down",
        "album_artist_id": "5030128490104968038",
        "genre": "Rock",
        "year": 2002,
        "track_number": 2,
        "disc_number": 1,
        "length_ms": 233278,
        "play_count": 0,
        "media_kind": "music",
        "data_kind": "file",
        "path": "/music/srv/Away From the Sun/02 Away From the Sun.mp3",
        "uri": "library:track:215"
      }
    ],
    "total": 14,
    "offset": 0,
    "limit": 2
  },
  "artists": {
    "items": [
      {
        "id": "8737690491750445895",
        "name": "The xx",
        "name_sort": "xx, The",
        "album_count": 2,
        "track_count": 25,
        "length_ms": 5229196,
        "uri": "library:artist:8737690491750445895"
      }
    ],
    "total": 1,
    "offset": 0,
    "limit": 2
  },
  "albums": {
    "items": [
      {
        "id": "8264078270267374619",
        "name": "Away From the Sun",
        "name_sort": "Away From the Sun",
        "artist": "3 Doors Down",
        "artist_id": "5030128490104968038",
        "track_count": 12,
        "length_ms": 2818174,
        "uri": "library:album:8264078270267374619"
      },
      {
        "id": "6835720495312674468",
        "name": "The Better Life",
        "name_sort": "Better Life",
        "artist": "3 Doors Down",
        "artist_id": "5030128490104968038",
        "track_count": 11,
        "length_ms": 2393332,
        "uri": "library:album:6835720495312674468"
      }
    ],
    "total": 3,
    "offset": 0,
    "limit": 2
  },
  "playlists": {
    "items": [],
    "total": 0,
    "offset": 0,
    "limit": 2
  }
}
```

### Search by query language

Search for artists, albums, tracks by a smart playlist query expression (see [README_SMARTPL.md](https://github.com/owntone/owntone-server/blob/master/README_SMARTPL.md) for the expression syntax).

**Endpoint**

```http
GET /api/search
```

**Query parameters**

| Parameter       | Value                                                       |
| --------------- | ----------------------------------------------------------- |
| expression      | The smart playlist query expression                         |
| type            | Comma separated list of the result types (`artist`, `album`, `track` |
| offset          | *(Optional)* Offset of the first item to return for each type |
| limit           | *(Optional)* Maximum number of items to return for each type  |

**Response**

| Key             | Type     | Value                                     |
| --------------- | -------- | ----------------------------------------- |
| tracks          | object   | [`paging`](#paging-object) object containing [`track`](#track-object) objects that matches the `query` |
| artists         | object   | [`paging`](#paging-object) object containing [`artist`](#artist-object) objects that matches the `query` |
| albums          | object   | [`paging`](#paging-object) object containing [`album`](#album-object) objects that matches the `query` |


**Example**

Search for music tracks ordered descending by the time added to the library and limit result to 2 items:

```shell
curl -X GET "http://localhost:3689/api/search?type=tracks&expression=media_kind+is+music+order+by+time_added+desc&offset=0&limit=2"
```


## Server info

| Method    | Endpoint                                         | Description                          |
| --------- | ------------------------------------------------ | ------------------------------------ |
| GET       | [/api/config](#config)                           | Get configuration information        |



### Config

**Endpoint**

```http
GET /api/config
```

**Response**

| Key             | Type     | Value                                     |
| --------------- | -------- | ----------------------------------------- |
| version         | string   | Server version                            |
| websocket_port  | integer  | Port number for the [websocket](#push-notifications) (or `0` if websocket is disabled) |
| buildoptions    | array    | Array of strings indicating which features are supported by the server |


**Example**

```shell
curl -X GET "http://localhost:3689/api/config"
```

```json
{
  "websocket_port": 3688,
  "version": "25.0",
  "buildoptions": [
    "ffmpeg",
    "iTunes XML",
    "Spotify",
    "LastFM",
    "MPD",
    "Device verification",
    "Websockets",
    "ALSA"
  ]
}
```


## Settings

| Method    | Endpoint                                         | Description                          |
| --------- | ------------------------------------------------ | ------------------------------------ |
| GET       | [/api/settings](#list-categories)                | Get all available categories         |
| GET       | [/api/settings/{category-name}](#get-a-category) | Get all available options for a category   |
| GET       | [/api/settings/{category-name}/{option-name}](#get-an-option) | Get a single setting option    |
| PUT       | [/api/settings/{category-name}/{option-name}](#change-an-option-value) | Change the value of a setting option    |
| DELETE    | [/api/settings/{category-name}/{option-name}](#delete-an-option) | Reset a setting option to its default   |



### List categories

List all settings categories with their options

**Endpoint**

```http
GET /api/settings
```

**Response**

| Key             | Type     | Value                                     |
| --------------- | -------- | ----------------------------------------- |
| categories      | array    | Array of settings [category](#category-object) objects |


**Example**

```shell
curl -X GET "http://localhost:3689/api/settings"
```

```json
{
  "categories": [
    {
      "name": "webinterface",
      "options": [
        {
          "name": "show_composer_now_playing",
          "type": 1,
          "value": true
        },
        {
          "name": "show_composer_for_genre",
          "type": 2,
          "value": "classical"
        }
      ]
    }
  ]
}
```


### Get a category

Get a settings category with their options

**Endpoint**

```http
GET /api/settings/{category-name}
```

**Response**

Returns a settings [category](#category-object) object


**Example**

```shell
curl -X GET "http://localhost:3689/api/settings/webinterface"
```

```json
{
  "name": "webinterface",
  "options": [
    {
      "name": "show_composer_now_playing",
      "type": 1,
      "value": true
    },
    {
      "name": "show_composer_for_genre",
      "type": 2,
      "value": "classical"
    }
  ]
}
```


### Get an option

Get a single settings option

**Endpoint**

```http
GET /api/settings/{category-name}/{option-name}
```

**Response**

Returns a settings [option](#option-object) object


**Example**

```shell
curl -X GET "http://localhost:3689/api/settings/webinterface/show_composer_now_playing"
```

```json
{
  "name": "show_composer_now_playing",
  "type": 1,
  "value": true
}
```


### Change an option value

Get a single settings option

**Endpoint**

```http
PUT /api/settings/{category-name}/{option-name}
```

**Request**

| Key             | Type     | Value                                     |
| --------------- | -------- | ----------------------------------------- |
| name            | string   | Option name |
| value           | (integer / boolean / string)   | New option value    |

**Response**

On success returns the HTTP `204 No Content` success status response code.


**Example**

```shell
curl -X PUT "http://localhost:3689/api/settings/webinterface/show_composer_now_playing" --data "{\"name\":\"show_composer_now_playing\",\"value\":true}"
```


### Delete an option

Delete a single settings option (thus resetting it to default)

**Endpoint**

```http
DELETE /api/settings/{category-name}/{option-name}
```

**Response**

On success returns the HTTP `204 No Content` success status response code.


**Example**

```shell
curl -X DELETE "http://localhost:3689/api/settings/webinterface/show_composer_now_playing"
```


## Push notifications

If the server was built with websocket support it exposes a websocket at `localhost:3688` to inform clients of changes (e. g. player state or library updates).
The port depends on the server configuration and can be read using the [`/api/config`](#config) endpoint.

After connecting to the websocket, the client should send a message containing the event types it is interested in. After that the server
will send a message each time one of the events occurred.

**Message**

| Key             | Type     | Value                                     |
| --------------- | -------- | ----------------------------------------- |
| notify          | array    | Array of event types                      |

**Event types**

| Type            | Description                               |
| --------------- | ----------------------------------------- |
| update          | Library update started or finished        |
| database        | Library database changed (new/modified/deleted tracks)  |
| outputs         | An output was enabled or disabled         |
| player          | Player state changes                      |
| options         | Playback option changes (shuffle, repeat, consume mode) |
| volume          | Volume changes                            |
| queue           | Queue changes                             |

**Example**

```shell
curl --include \
     --no-buffer \
     --header "Connection: Upgrade" \
     --header "Upgrade: websocket" \
     --header "Host: localhost:3688" \
     --header "Origin: http://localhost:3688" \
     --header "Sec-WebSocket-Key: SGVsbG8sIHdvcmxkIQ==" \
     --header "Sec-WebSocket-Version: 13" \
     --header "Sec-WebSocket-Protocol: notify" \
     http://localhost:3688/ \
     --data "{ \"notify\": [ \"player\" ] }"
```

```json
{ 
  "notify": [
    "player"
  ]
}
```


## Object model


### `queue item` object

| Key                | Type     | Value                                     |
| ------------------ | -------- | ----------------------------------------- |
| id                 | string   | Item id                                   |
| position           | integer  | Position in the queue (starting with zero) |
| track_id           | string   | Track id                                   |
| title              | string   | Title                                     |
| artist             | string   | Track artist name                         |
| artist_sort        | string   | Track artist sort name                    |
| album              | string   | Album name                                |
| album_sort         | string   | Album sort name                           |
| album_id           | string   | Album id                                  |
| album_artist       | string   | Album artist name                         |
| album_artist_sort  | string   | Album artist sort name                    |
| album_artist_id    | string   | Album artist id                           |
| composer           | string   | Composer (optional)                       |
| genre              | string   | Genre                                     |
| year               | integer  | Release year                              |
| track_number       | integer  | Track number                              |
| disc_number        | integer  | Disc number                               |
| length_ms          | integer  | Track length in milliseconds              |
| media_kind         | string   | Media type of this track: `music`, `movie`, `podcast`, `audiobook`, `musicvideo`, `tvshow` |
| data_kind          | string   | Data type of this track: `file`, `url`, `spotify`, `pipe` |
| path               | string   | Path                                      |
| uri                | string   | Resource identifier                       |
| artwork_url        | string   | *(optional)* [Artwork url](#artwork-urls) |
| type               | string   | file (codec) type (ie mp3/flac/...)       |
| bitrate            | string   | file bitrate (ie 192/128/...)             |
| samplerate         | string   | file sample rate (ie 44100/48000/...)     |
| channel            | string   | file channel (ie mono/stereo/xx ch))      |


### `playlist` object

| Key             | Type     | Value                                     |
| --------------- | -------- | ----------------------------------------- |
| id              | string   | Playlist id                               |
| name            | string   | Playlist name                             |
| path            | string   | Path                                      |
| parent_id       | integer  | Playlist id of the parent (folder) playlist |
| type            | string   | Type of this playlist: `special`, `folder`, `smart`, `plain` |
| smart_playlist  | boolean  | `true` if playlist is a smart playlist    |
| folder          | boolean  | `true` if it is a playlist folder         |
| uri             | string   | Resource identifier                       |


### `artist` object

| Key             | Type     | Value                                     |
| --------------- | -------- | ----------------------------------------- |
| id              | string   | Artist id                                 |
| name            | string   | Artist name                               |
| name_sort       | string   | Artist sort name                          |
| album_count     | integer  | Number of albums                          |
| track_count     | integer  | Number of tracks                          |
| length_ms       | integer  | Total length of tracks in milliseconds    |
| uri             | string   | Resource identifier                       |
| artwork_url     | string   | *(optional)* [Artwork url](#artwork-urls) |


### `album` object

| Key             | Type     | Value                                     |
| --------------- | -------- | ----------------------------------------- |
| id              | string   | Album id                                  |
| name            | string   | Album name                                |
| name_sort       | string   | Album sort name                           |
| artist_id       | string   | Album artist id                           |
| artist          | string   | Album artist name                         |
| track_count     | integer  | Number of tracks                          |
| length_ms       | integer  | Total length of tracks in milliseconds    |
| uri             | string   | Resource identifier                       |
| artwork_url     | string   | *(optional)* [Artwork url](#artwork-urls) |


### `track` object

| Key                | Type     | Value                                     |
| ------------------ | -------- | ----------------------------------------- |
| id                 | integer  | Track id                                  |
| title              | string   | Title                                     |
| title_sort         | string   | Sort title                                |
| artist             | string   | Track artist name                         |
| artist_sort        | string   | Track artist sort name                    |
| album              | string   | Album name                                |
| album_sort         | string   | Album sort name                           |
| album_id           | string   | Album id                                  |
| album_artist       | string   | Album artist name                         |
| album_artist_sort  | string   | Album artist sort name                    |
| album_artist_id    | string   | Album artist id                           |
| composer           | string   | Track composer                            |
| genre              | string   | Genre                                     |
| comment            | string   | Comment                                   |
| year               | integer  | Release year                              |
| track_number       | integer  | Track number                              |
| disc_number        | integer  | Disc number                               |
| length_ms          | integer  | Track length in milliseconds              |
| rating             | integer  | Track rating (ranges from 0 to 100)       |
| play_count         | integer  | How many times the track was played       |
| skip_count         | integer  | How many times the track was skipped      |
| time_played        | string   | Timestamp in `ISO 8601` format           |
| time_skipped       | string   | Timestamp in `ISO 8601` format           |
| time_added         | string   | Timestamp in `ISO 8601` format           |
| date_released      | string   | Date in the format `yyyy-mm-dd`         |
| seek_ms            | integer  | Resume point in milliseconds (available only for podcasts and audiobooks) |
| media_kind         | string   | Media type of this track: `music`, `movie`, `podcast`, `audiobook`, `musicvideo`, `tvshow` |
| data_kind          | string   | Data type of this track: `file`, `url`, `spotify`, `pipe` |
| path               | string   | Path                                      |
| uri                | string   | Resource identifier                       |
| artwork_url        | string   | *(optional)* [Artwork url](#artwork-urls) |
| usermark           | integer  | User review marking of track (ranges from 0) |


### `paging` object

| Key             | Type     | Value                                     |
| --------------- | -------- | ----------------------------------------- |
| items           | array    | Array of result objects                   |
| total           | integer  | Total number of items                     |
| offset          | integer  | Requested offset of the first item        |
| limit           | integer  | Requested maximum number of items         |


### `browse-info` object

| Key             | Type     | Value                                     |
| --------------- | -------- | ----------------------------------------- |
| name            | string   | Name (depends on the type of the query)   |
| name_sort       | string   | Sort name                                 |
| artist_count    | integer  | Number of artists                         |
| album_count     | integer  | Number of albums                          |
| track_count     | integer  | Number of tracks                          |
| time_played     | string   | Timestamp in `ISO 8601` format           |
| time_added      | string   | Timestamp in `ISO 8601` format           |


### `directory` object

| Key             | Type     | Value                                     |
| --------------- | -------- | ----------------------------------------- |
| path            | string   | Directory path                            |


### `category` object

| Key             | Type     | Value                                     |
| --------------- | -------- | ----------------------------------------- |
| name            | string   | Category name                             |
| options         | array    | Array of option in this category          |


### `option` object

| Key             | Type     | Value                                     |
| --------------- | -------- | ----------------------------------------- |
| name            | string   | Option name                               |
| type            | integer  | The type of the value for this option (`0`: integer, `1`: boolean, `2`: string) |
| value           | (integer / boolean / string)  | Current value for this option                               |


### Artwork urls

Artwork urls in `queue item`, `artist`, `album` and `track` objects can be either relative urls or absolute urls to the artwork image.
Absolute artwork urls are pointing to external artwork images (e. g. for radio streams that provide artwork metadata), while relative artwork urls are served from the server.

It is possible to add the query parameters `maxwidth` and/or `maxheight` to relative artwork urls, in order to get a smaller image (the server only scales down never up).

Note that even if a relative artwork url attribute is present, it is not guaranteed to exist.
