# forked-daapd API Endpoint Reference

Available API endpoints:

* [Player](#player): control playback, volume, shuffle/repeat modes
* [Outputs / Speakers](#outputs--speakers): list available outputs and enable/disable outputs
* [Queue](#queue): list, add or modify the current queue
* [Library](#library): list playlists, artists, albums and tracks from your library
* [Search](#search): search for playlists, artists, albums and tracks
* [Server info](#server-info): get server information
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
| PUT       | [/api/player/play, /api/player/pause, /api/player/stop](#control-playback) | Start, pause or stop playback |
| PUT       | [/api/player/next, /api/player/prev](#skip-tracks) | Skip forward or backward           |
| PUT       | [/api/player/shuffle](#set-shuffle-mode)         | Set shuffle mode                     |
| PUT       | [/api/player/consume](#set-consume-mode)         | Set consume mode                     |
| PUT       | [/api/player/repeat](#set-repeat-mode)           | Set repeat mode                      |
| PUT       | [/api/player/volume](#set-volume)                | Set master volume or volume for a specific output |
| PUT       | [/api/player/seek](#seek)                        | Seek to a position in the currently playing track |



### Get player status

**Endpoint**

```
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

```
curl -X GET "http://localhost:3689/api/player"
```

```
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

```
PUT /api/player/play
```

```
PUT /api/player/pause
```

```
PUT /api/player/stop
```

**Response**

On success returns the HTTP `204 No Content` success status response code.

**Example**

```
curl -X PUT "http://localhost:3689/api/player/play"
```

```
curl -X PUT "http://localhost:3689/api/player/pause"
```

```
curl -X PUT "http://localhost:3689/api/player/stop"
```


### Skip tracks

Skip forward or backward

**Endpoint**

```
PUT /api/player/next
```

```
PUT /api/player/prev
```

**Response**

On success returns the HTTP `204 No Content` success status response code.

**Example**

```
curl -X PUT "http://localhost:3689/api/player/next"
```

```
curl -X PUT "http://localhost:3689/api/player/prev"
```


### Set shuffle mode

Enable or disable shuffle mode

**Endpoint**

```
PUT /api/player/shuffle
```

**Query parameters**

| Parameter       | Value                                                       |
| --------------- | ----------------------------------------------------------- |
| state           | The new shuffle state, should be either `true` or `false` |


**Response**

On success returns the HTTP `204 No Content` success status response code.

**Example**

```
curl -X PUT "http://localhost:3689/api/player/shuffle?state=true"
```


### Set consume mode

Enable or disable consume mode

**Endpoint**

```
PUT /api/player/consume
```

**Query parameters**

| Parameter       | Value                                                       |
| --------------- | ----------------------------------------------------------- |
| state           | The new consume state, should be either `true` or `false` |


**Response**

On success returns the HTTP `204 No Content` success status response code.

**Example**

```
curl -X PUT "http://localhost:3689/api/player/consume?state=true"
```


### Set repeat mode

Change repeat mode

**Endpoint**

```
PUT /api/player/repeat
```

**Query parameters**

| Parameter       | Value                                                       |
| --------------- | ----------------------------------------------------------- |
| state           | The new repeat mode, should be either `off`, `all` or `single` |


**Response**

On success returns the HTTP `204 No Content` success status response code.

**Example**

```
curl -X PUT "http://localhost:3689/api/player/repeat?state=all"
```


### Set volume

Change master volume or volume of a specific output.

**Endpoint**

```
PUT /api/player/volume
```

**Query parameters**

| Parameter       | Value                                                       |
| --------------- | ----------------------------------------------------------- |
| volume          | The new volume (0 - 100)                                    |
| output_id       | *(Optional)* If an output id is given, only the volume of this output will be changed. If parameter is omited, the master volume will be changed. |


**Response**

On success returns the HTTP `204 No Content` success status response code.

**Example**

```
curl -X PUT "http://localhost:3689/api/player/volume?volume=50"
```

```
curl -X PUT "http://localhost:3689/api/player/volume?volume=50&output_id=0"
```


### Seek

Seek to a position in the currently playing track.

**Endpoint**

```
PUT /api/player/seek
```

**Query parameters**

| Parameter       | Value                                                       |
| --------------- | ----------------------------------------------------------- |
| position_ms     | The new position in milliseconds to seek to                 |


**Response**

On success returns the HTTP `204 No Content` success status response code.

**Example**

```
curl -X PUT "http://localhost:3689/api/player/seek?position_ms=2000"
```



## Outputs / Speakers

| Method    | Endpoint                                         | Description                          |
| --------- | ------------------------------------------------ | ------------------------------------ |
| GET       | [/api/outputs](#get-a-list-of-available-outputs) | Get a list of available outputs      |
| PUT       | [/api/outputs/set](#set-enabled-outputs)         | Set enabled outputs                  |
| GET       | [/api/outputs/{id}](#get-an-output)              | Get an output                        |
| PUT       | [/api/outputs/{id}](#change-an-output)           | Change an output (enable/disable or volume) |



### Get a list of available outputs

**Endpoint**

```
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

```
curl -X GET "http://localhost:3689/api/outputs"
```

```
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

Set the enabled outputs by passing an array of output ids. forked-daapd enables all outputs
with the given ids and disables the remaining outputs.

**Endpoint**

```
PUT /api/outputs/set
```

**Body parameters**

| Parameter       | Type     | Value                |
| --------------- | -------- | -------------------- |
| outputs         | array    | Array of output ids  |

**Response**

On success returns the HTTP `204 No Content` success status response code.

**Example**

```
curl -X PUT "http://localhost:3689/api/outputs/set" --data "{\"outputs\":[\"198018693182577\",\"0\"]}"
```


### Get an output

Get an output

**Endpoint**

```
GET /api/outputs/{id}
```

**Path parameters**

| Parameter       | Value                |
| --------------- | -------------------- |
| id              | Output id            |

**Response**

On success returns the HTTP `200 OK` success status response code. With the response body holding the **`output` object**.

**Example**

```
curl -X GET "http://localhost:3689/api/outputs/0"
```

```
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

```
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

**Response**

On success returns the HTTP `204 No Content` success status response code.

**Example**

```
curl -X PUT "http://localhost:3689/api/outputs/0" --data "{\"selected\":true, \"volume\": 50}"
```



## Queue

| Method    | Endpoint                                                    | Description                          |
| --------- | ----------------------------------------------------------- | ------------------------------------ |
| GET       | [/api/queue](#list-queue-items)                             | Get a list of queue items            |
| PUT       | [/api/queue/clear](#clearing-the-queue)                     | Remove all items from the queue      |
| POST      | [/api/queue/items/add](#adding-items-to-the-queue)          | Add items to the queue               |
| PUT       | [/api/queue/items/{id}](#moving-a-queue-item)               | Move a queue item in the queue       |
| DELETE    | [/api/queue/items/{id}](#removing-a-queue-item)             | Remove a queue item form the queue   |



### List queue items

Lists the items in the current queue

**Endpoint**

```
GET /api/queue
```

**Query parameters**

| Parameter       | Value                                                       |
| --------------- | ----------------------------------------------------------- |
| id              | *(Optional)* If a queue item id is given, only the item with the id will be returend. |
| start           | *(Optional)* If a `start`and an `end` position is given, only the items from `start` (included) to `end` (excluded) will be returned. If only a `start` position is given, only the item at this position will be returned. |
| end             | *(Optional)* See `start` parameter |

**Response**

| Key             | Type     | Value                                     |
| --------------- | -------- | ----------------------------------------- |
| version         | integer  | Version number of the current queue       |
| count           | integer  | Number of items in the current queue      |
| items           | array    | Array of [`queue item`](#queue-item-object) objects |

**Example**

```
curl -X GET "http://localhost:3689/api/queue"
```

```
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

```
PUT /api/queue/clear
```

**Response**

On success returns the HTTP `204 No Content` success status response code.

**Example**

```
curl -X PUT "http://localhost:3689/api/queue/clear"
```


### Adding items to the queue

Add tracks, playlists artists or albums to the current queue

**Endpoint**

```
POST /api/queue/items/add
```

**Query parameters**

| Parameter       | Value                                                       |
| --------------- | ----------------------------------------------------------- |
| uris            | Comma seperated list of resource identifiers (`track`, `playlist`, `artist` or `album` object `uri`) |

**Response**

On success returns the HTTP `204 No Content` success status response code.

**Example**

```
curl -X POST "http://localhost:3689/api/queue/items/add?uris=library:playlist:68,library:artist:2932599850102967727"
```


### Moving a queue item

Move a queue item in the current queue

**Endpoint**

```
PUT /api/queue/items/{id}
```

**Path parameters**

| Parameter       | Value                |
| --------------- | -------------------- |
| id              | Queue item id        |

**Query parameters**

| Parameter       | Value                                                       |
| --------------- | ----------------------------------------------------------- |
| new_position    | The new position for the queue item in the current queue.   |

**Response**

On success returns the HTTP `204 No Content` success status response code.

**Example**

```
curl -X PUT "http://localhost:3689/api/queue/items/3?new_position=0"
```


### Removing a queue item

Remove a queue item from the current queue

**Endpoint**

```
DELETE /api/queue/items/{id}
```

**Path parameters**

| Parameter       | Value                |
| --------------- | -------------------- |
| id              | Queue item id        |

**Response**

On success returns the HTTP `204 No Content` success status response code.

**Example**

```
curl -X PUT "http://localhost:3689/api/queue/items/2"
```



## Library

| Method    | Endpoint                                                    | Description                          |
| --------- | ----------------------------------------------------------- | ------------------------------------ |
| GET       | [/api/library/playlists](#list-playlists)                   | Get a list of playlists              |
| GET       | [/api/library/playlists/{id}](#get-a-playlist)              | Get a playlist                       |
| GET       | [/api/library/playlists/{id}/tracks](#list-playlist-tracks) | Get list of tracks for a playlist    |
| GET       | [/api/library/artists](#list-artists)                       | Get a list of artists                |
| GET       | [/api/library/artists/{id}](#get-an-artist)                 | Get an artist                        |
| GET       | [/api/library/artists/{id}/albums](#list-artist-albums)     | Get list of albums for an artist     |
| GET       | [/api/library/albums](#list-albums)                         | Get a list of albums                 |
| GET       | [/api/library/albums/{id}](#get-an-album)                   | Get an album                         |
| GET       | [/api/library/albums/{id}/tracks](#list-album-tracks)       | Get list of tracks for an album      |
| GET       | [/api/library/count](#get-count-of-tracks-artists-and-albums) | Get count of tracks, artists and albums |



### List playlists

Lists the playlists in your library

**Endpoint**

```
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

```
curl -X GET "http://localhost:3689/api/library/playlists"
```

```
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

```
GET /api/library/playlists/{id}
```

**Path parameters**

| Parameter       | Value                |
| --------------- | -------------------- |
| id              | Playlist id          |

**Response**

On success returns the HTTP `200 OK` success status response code. With the response body holding the **[`playlist`](#playlist-object) object**.


**Example**

```
curl -X GET "http://localhost:3689/api/library/playlists/1"
```

```
{
  "id": 1,
  "name": "radio",
  "path": "/music/srv/radio.m3u",
  "smart_playlist": false,
  "uri": "library:playlist:1"
}
```


### List playlist tracks

Lists the tracks in a playlists

**Endpoint**

```
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

```
curl -X GET "http://localhost:3689/api/library/playlists/1/tracks"
```

```
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


### List artists

Lists the artists in your library

**Endpoint**

```
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

```
curl -X GET "http://localhost:3689/api/library/artists"
```

```
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

```
GET /api/library/artists/{id}
```

**Path parameters**

| Parameter       | Value                |
| --------------- | -------------------- |
| id              | Artist id            |

**Response**

On success returns the HTTP `200 OK` success status response code. With the response body holding the **[`artist`](#artist-object) object**.


**Example**

```
curl -X GET "http://localhost:3689/api/library/artists/3815427709949443149"
```

```
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

```
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

```
curl -X GET "http://localhost:3689/api/library/artists/32561671101664759/albums"
```

```
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

```
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

```
curl -X GET "http://localhost:3689/api/library/albums"
```

```
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

```
GET /api/library/albums/{id}
```

**Path parameters**

| Parameter       | Value                |
| --------------- | -------------------- |
| id              | Album id             |

**Response**

On success returns the HTTP `200 OK` success status response code. With the response body holding the **[`album`](#album-object) object**.


**Example**

```
curl -X GET "http://localhost:3689/api/library/albums/8009851123233197743"
```

```
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

```
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

```
curl -X GET "http://localhost:3689/api/library/albums/1/tracks"
```

```
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


### Get count of tracks, artists and albums

Get information about the number of tracks, artists and albums and the total playtime

**Endpoint**

```
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

```
curl -X GET "http://localhost:3689/api/library/count?expression=data_kind+is+file"
```

```
{
  "tracks": 6811,
  "artists": 355,
  "albums": 646,
  "db_playtime": 1590767
}
```



## Search

| Method    | Endpoint                                                    | Description                          |
| --------- | ----------------------------------------------------------- | ------------------------------------ |
| GET       | [/api/search](#search-by-search-term)                       | Search for playlists, artists, albums, tracks by a simple search term |
| GET       | [/api/search](#search-by-query-language)                    | Search by complex query expression   |



### Search by search term

Search for playlists, artists, albums, tracks that include the given query in their title (case insensitive matching).

**Endpoint**

```
GET /api/search
```

**Query parameters**

| Parameter       | Value                                                       |
| --------------- | ----------------------------------------------------------- |
| query           | The search keyword                                          |
| type            | Comma separated list of the result types (`playlist`, `artist`, `album`, `track`) |
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

```
curl -X GET "http://localhost:3689/api/search?type=tracks,artists,albums,playlists&query=the&offset=0&limit=2"
```

```
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

Search for artists, albums, tracks by a smart playlist query expression (see [README_SMARTPL.md](https://github.com/ejurgensen/forked-daapd/blob/master/README_SMARTPL.md) for the expression syntax).

**Endpoint**

```
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

```
curl -X GET "http://localhost:3689/api/search?type=tracks&expression=media_kind+is+music+order+by+time_added+desc&offset=0&limit=2"
```


## Server info

| Method    | Endpoint                                         | Description                          |
| --------- | ------------------------------------------------ | ------------------------------------ |
| GET       | [/api/config](#config)                           | Get configuration information        |



### Config

**Endpoint**

```
GET /api/config
```

**Response**

| Key             | Type     | Value                                     |
| --------------- | -------- | ----------------------------------------- |
| version         | string   | forked-daapd server version               |
| websocket_port  | integer  | Port number for the [websocket](#push-notifications) (or `0` if websocket is disabled) |
| buildoptions    | array    | Array of strings indicating which features are supported by the forked-daapd server |


**Example**

```
curl -X GET "http://localhost:3689/api/config"
```

```
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

## Push notifications

If forked-daapd was built with websocket support, forked-daapd exposes a websocket at `localhost:3688` to inform clients of changes (e. g. player state or library updates).
The port depends on the forked-daapd configuration and can be read using the [`/api/config`](#config) endpoint.

After connecting to the websocket, the client should send a message containing the event types it is interested in. After that forked-daapd
will send a message each time one of the events occurred.

**Message**

| Key             | Type     | Value                                     |
| --------------- | -------- | ----------------------------------------- |
| notify          | array    | Array of event types                      |

**Event types**

| Type            | Description                               |
| --------------- | ----------------------------------------- |
| update          | Library update started or finished        |
| outputs         | An output was enabled or disabled         |
| player          | Player state changes                      |
| options         | Playback option changes (shuffle, repeat, consume mode) |
| volume          | Volume changes                            |
| queue           | Queue changes                             |

**Example**

```
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

```
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
| album_artist       | string   | Album artist name                         |
| album_artist_sort  | string   | Album artist sort name                    |
| genre              | string   | Genre                                     |
| year               | integer  | Release year                              |
| track_number       | integer  | Track number                              |
| disc_number        | integer  | Disc number                               |
| length_ms          | integer  | Track length in milliseconds              |
| media_kind         | string   | Media type of this track: `music`, `movie`, `podcast`, `audiobook`, `musicvideo`, `tvshow` |
| data_kind          | string   | Data type of this track: `file`, `url`, `spotify`, `pipe` |
| path               | string   | Path                                      |
| uri                | string   | Resource identifier                       |


### `playlist` object

| Key             | Type     | Value                                     |
| --------------- | -------- | ----------------------------------------- |
| id              | string   | Playlist id                               |
| name            | string   | Playlist name                             |
| path            | string   | Path                                      |
| smart_playlist  | boolean  | `true` if playlist is a smart playlist   |
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


### `track` object

| Key                | Type     | Value                                     |
| ------------------ | -------- | ----------------------------------------- |
| id                 | string   | Track id                                  |
| title              | string   | Title                                     |
| artist             | string   | Track artist name                         |
| artist_sort        | string   | Track artist sort name                    |
| album              | string   | Album name                                |
| album_sort         | string   | Album sort name                           |
| album_id           | string   | Album id                                  |
| album_artist       | string   | Album artist name                         |
| album_artist_sort  | string   | Album artist sort name                    |
| album_artist_id    | string   | Album artist id                           |
| genre              | string   | Genre                                     |
| year               | integer  | Release year                              |
| track_number       | integer  | Track number                              |
| disc_number        | integer  | Disc number                               |
| length_ms          | integer  | Track length in milliseconds              |
| play_count         | integer  | How many times the track was played       |
| time_played        | string   | Timestamp in `ISO 8601` format           |
| time_added         | string   | Timestamp in `ISO 8601` format           |
| date_released      | string   | Date in the format `yyyy-mm-dd`         |
| seek_ms            | integer  | Resume point in milliseconds (available only for podcasts and audiobooks) |
| media_kind         | string   | Media type of this track: `music`, `movie`, `podcast`, `audiobook`, `musicvideo`, `tvshow` |
| data_kind          | string   | Data type of this track: `file`, `stream`, `spotify`, `pipe` |
| path               | string   | Path                                      |
| uri                | string   | Resource identifier                       |


### `paging` object

| Key             | Type     | Value                                     |
| --------------- | -------- | ----------------------------------------- |
| items           | array    | Array of result objects                   |
| total           | integer  | Total number of items                     |
| offset          | integer  | Requested offset of the first item        |
| limit           | integer  | Requested maximum number of items         |

