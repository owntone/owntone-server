# forked-daapd API Endpoint Reference

Available API endpoints:

* [Player](#player): control playback, volume, shuffle/repeat modes
* [Outputs / Speakers](#outputs--speakers): list available outputs and enable/disable outputs
* [Server info](#server-info): get server information
* [Push notifications](#push-notifications): receive push notifications

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
