# Configuration

The configuration of OwnTone - usually located in `/etc/owntone.conf` - is split into multiple sections:

- [`general`](#general-settings) - Main settings of OwnTone.
- [`library`](#library-settings) - Settings of local library.
- [`audio`](#local-audio-settings) - Settings for the local audio.
- [`alsa`](#per-alsa-device-settings) - Settings for ALSA devices.
- [`fifo`](#fifo-settings) - Settings for named pipe.
- [`airplay_shared`](#shared-airplay-settings) - Settings shared across Airplay devices.
- [`airplay`](#per-airplay-device-settings) - Settings for a specific Airplay device.
- [`chromecast`](#per-chromecast-device-settings) - Settings for a specific Chromecast device.
- [`spotify`](#spotify-settings) - Settings for the Spotify playback.
- [`rcp`](#rcp--roku-soundbridge-settings) - Settings for RCP / Roku Soundbridge devices.
- [`mpd`](#mpd-settings) - Settings for MPD clients.
- [`sqlite`](#sqlite-settings) - Settings for SQLite operation.
- [`streaming`](#streaming-settings) - Settings for the streaming.

## Format

Each section consists of a name enclosing settings within parentheses.

Each setting consists of a name and a value. There are different types of settings: string, integer, boolean, and list.

Comments are preceded by a hash sign.

The format is as follow:

```conf
# Section
section {
    # String value
    setting = "<string-value>"
    # Integer value
    setting = <integer-value>
    # Boolean
    setting = <true|false>
    # List
    setting = { "value a", "value b", "value n"}
}
```

**Note:** For a regular use, the most important settings are:

- the `directories` (see [`library`](#library-settings) section), which should be the location of your media, and
- the `uid` (see [`general`](#general-settings) section), which must have read access to those directories.

## General Settings

```conf
general {
  ...
}
```

The `general` section accepts the settings below.

- `uid` - Identifier of the user running OwnTone.

  **Notes:**

  - Make sure that this user has read access to the `directories` ([`library`](#library-settings) section) section and write access to the database (`db_path`), log file (`logfile`) and local audio ([`audio`](#local-audio-settings) section).
  - This setting is mandatory.

  **Default:** `"nobody"`

  ```conf
  uid = "<user-identifier>"
  ```

- `db_path` - Full path to the database file.

  **Note:** This setting is mandatory.

  **Default:** `"/var/cache/owntone/songs3.db"`

  ```conf
  db_path = "<path-to-database-file>"
  ```

- `db_backup_path` - Full path to the database file backup.

  **Note:** Backups are triggered from an API endpoint.

  **Default:** unset

  ```conf
  db_backup_path = "<path-to-database-backup-file>"
  ```

- `logfile` - Full path to the log file.

  **Default:** `"/var/log/owntone.log"`

  ```conf
  logfile = "<path-to-log-file>"
  ```

- `loglevel` - Level of verbosity of the logging.

  **Note:** There are 6 levels of verbosity (hereunder from the less verbose to the most verbose). The level `log` is recommended for regular usage.

  **Valid values:** `fatal`, `log`, `warning`, `info`, `debug`, `spam`

  **Default:** `"log"`

  ```conf
  loglevel = "<fatal|log|warning|info|debug|spam>"
  ```

- `admin_password` - Password for the web interface.

  **Note:** If a user is accessing the web interface from a device located in one of the `trusted_networks`, no password is required.

  **Default:** unset

  ```conf
  admin_password = "<password>"
  ```

- `websocket_port` - Port number used to answer requests from the web interface.

  **Default:** `3688`

  ```conf
  websocket_port = <port-number>
  ```

- `websocket_interface` - Network interface on which the web socket is listening: e.g., eth0, en0.

  **Note:** When this setting is unset, it means that the web socket listens on all available interfaces.

  **Default:** unset

  ```conf
  websocket_interface = "<interface>"
  ```

- `trusted_networks` - List of networks considered safe to access OwnTone without authorisation (see also `admin_password`).

  **Note:** This applies to these client types: remotes, DAAP clients (e.g., Apple Music, iTunes) and the web interface.

  **Valid values:** `any`, `localhost`, or the prefix to one or more IP networks.

  **Default:** `{ "localhost", "192.168", "fd" }`

  ```conf
  trusted_networks = { <"any"|"localhost"|"ip-range-prefix">, <...> }
  ```

- `ipv6` - Flag to indicate whether or not IPv6 must used.

  **Default:** `true`

  ```conf
  ipv6 = <true|false>
  `
  ```

- `bind_address` - Specific IP address to which the server is bound.

  **Note:** It can be an IPv4 or IPv6 address and by default the server listens on all IP addresses.

  **Default:** unset

  ```conf
  bind_address = "<ip-address>"
  ```

- `cache_path` - Full path to the cache database file.

  **Default:** unset

  ```conf
  cache_path = "<path-to-database-cache-file>"
  ```

- `cache_daap_threshold` - Threshold in milliseconds for DAAP requests.

  **Note:** Set to `0` to disable caching.

  **Default:** `1000`

  ```conf
  cache_daap_threshold = <threshold>
  ```

- `speaker_autoselect` - Flag to automatically select the speaker when starting the playback if none of the previously selected speakers / outputs are available.

  **Default:** `false`

  ```conf
  speaker_autoselect = <true|false>
  ```

- `high_resolution_clock` - Flag to indicate whether or not the high-resolution clock must be set.

  **Note:** Most modern operating systems have a high-resolution clock, but if OwnTone is running on an unusual platform and drop-outs are experienced, this setting set to `true`.

  **Default:** `false` on FreeBSD-based operating systems, `true` otherwise

  ```conf
  high_resolution_clock = <true|false>
  ```

## Library Settings

```conf
library {
  ...
}
```

The `library` section accepts the settings below.

- `name` - Name of the library as displayed by the clients.

  **Notes:**

  - If you change the name after pairing with Remote you may have to redo the pairing.
  - The place holder `%h` can be used to display the hostname.

  **Default:** `"My Music on %h"`

  ```conf
  name = "<library-name>"
  `
  ```

- `port` - TCP port to listen on.

  **Default:** `3689`

  ```conf
  port = 3689
  ```

- `password` - Password for the library.

  **Default:** unset

  ```conf
  password = "<password>"
  ```

- `directories` - Path to the directories containing the media to index.

  **Default:** unset

  ```conf
  directories = { "<path-to-media>", "<...>" }
  ```

- `follow_symlinks` - Flag to indicate whether or not symbolic links must be followed.

  **Default:** `true`.

  ```conf
  follow_symlinks = <true|false>
  ```

- `podcasts` - List of directories containing podcasts.

  **Note:** For each directory that is indexed, the path is matched against these names. If there is a match, all items in the directory are marked as podcasts. If you index `/srv/music`, and your podcasts are in `/srv/music/Podcasts`, then you can set this to `{ "/Podcasts" }`. Changing this setting only takes effect after a rescan.

  **Default:** unset

  ```conf
  podcasts = { "<podcast-directory>", "<...>" }
  ```

- `audiobooks` - List of directories containing audiobooks.

  **Note:** For each directory that is indexed, the path is matched against these names. If there is a match, all items in the directory are marked as audiobooks. If you index `/srv/music`, and your podcasts are in `/srv/music/Audiobooks`, then you can set this to `{ "/Audiobooks" }`.Changing this setting only takes effect after a rescan.

  **Default:** unset

  ```conf
  audiobooks = { "/Audiobooks" }
  ```

- `compilations` - List of directories containing compilations: e.g., greatest hits, best of, soundtracks.

  **Note:** For each directory that is indexed, the path is matched against these names. If there is a match, all items in the directory are marked as compilations.Changing this setting only takes effect after a rescan.

  **Default:** unset

  ```conf
  compilations = { "/Compilations" }
  ```

- `compilation_artist` - Artist name of compilation albums.

  **Note:** Compilations usually have multiple artists, and sometimes may have no album artist. If you don't want every artist to be listed, you can set a single name which will be used for all compilation tracks without an album artist, and for all tracks in the compilation directories. Changing this setting only takes effect after a rescan.

  **Default:** unset

  ```conf
  compilation_artist = "<various-artists>"
  ```

- `hide_singles` - Flag to indicate whether or not single albums must be hidden.

  **Note:** If your album and artist lists are cluttered, you can choose to hide albums and artists with only one track. The tracks will still be visible in other lists, e.g., tracks and playlists. This setting currently only works with some remotes.

  **Default:** `false`

  ```conf
  hide_singles = <true|false>
  `
  ```

- `radio_playlists` - Flag to show internet streams in normal playlists.

  **Note:** By default the internet streams are shown in the "Radio" library, like iTunes does. However, some clients (like TunesRemote+) won't show the "Radio" library.

  **Default:** `false`

  ```conf
  radio_playlists = <true|false>
  ```

- `name_library` - Name of the default playlist _Library_.

  **Note:** This is a default playlist, which can be renamed with this setting.

  **Default:** `"Library"`

  ```conf
  name_library = "<library-playlist-name>"
  ```

- `name_music` - Name of the default playlist _Music_.

  **Note:** This is a default playlist, which can be renamed with this setting.

  **Default:** `"Music"`

  ```conf
  name_music = "<music-playlist-name>"
  ```

- `name_movies` - Name of the default playlist _Movies_.

  **Note:** This is a default playlist, which can be renamed with this setting.

  **Default:** `"Movies"`

  ```conf
  name_movies = "<movies-playlist-name>"
  ```

- `name_tvshows` - Name of the default playlist _TV Shows_.

  **Note:** This is a default playlist, which can be renamed with this setting.

  **Default:** `"TV Shows"`

  ```conf
  name_tvshows = "<tv-shows-playlist-name>"
  ```

- `name_podcasts` - Name of the default playlist _Podcasts_.

  **Note:** This is a default playlist, which can be renamed with this setting.

  **Default:** `"Podcasts"`

  ```conf
  name_podcasts = "<podcasts-playlist-name>"
  ```

- `name_audiobooks` - Name of the default playlist _Audiobooks_.

  **Note:** This is a default playlist, which can be renamed with this setting.

  **Default:** `"Audiobooks"`

  ```conf
  name_audiobooks = "<audiobooks-playlist-name>"
  ```

- `name_radio` - Name of the default playlist _Radio_.

  **Note:** This is a default playlist, which can be renamed with this setting.

  **Default:** `"Radio"`

  ```conf
  name_radio = "<radio-playlist-name>"
  ```

- `name_unknown_title` - Name of tracks having an undefined title.

  **Default:** `"Unknown title"`

  ```conf
  name_unknown_title = "<unknown-title-name>"
  ```

- `name_unknown_artist` - Name of artist having an undefined name.

  **Default:** `"Unknown artist"`

  ```conf
  name_unknown_artist = "<unknown-artist-name>"
  ```

- `name_unknown_album` - Name of album having an undefined title.

  **Default:** `"Unknown album"`

  ```conf
  name_unknown_album = "<unknown-album-name>"
  ```

- `name_unknown_genre` - Name of genre having an undefined name.

  **Default:** `"Unknown genre"`

  ```conf
  name_unknown_genre = "<unknown-genre-name>"
  ```

- `name_unknown_composer` - Name of composer having an undefined name.

  **Default:** `"Unknown composer"`

  ```conf
  name_unknown_composer = "<unknown-composer-name>"
  ```

- `artwork_basenames` - List of base names for artwork files (file names without extension).

  **Note:**

  - OwnTone searches for JPEG and PNG files with these base names.
  - More information regarding artwork can be found [here](artwork.md).

  **Default:** `{ "artwork", "cover", "Folder" }`

  ```conf
  artwork_basenames = { "<file-name>", "<...>" }
  ```

- `artwork_individual` - Flag to indicate whether or not the search for artwork corresponding to each individual media file must be done instead of only looking for the album artwork.

  **Notes:**

  - Disable this setting to reduce cache size.
  - More information regarding artwork can be found [here](artwork.md).

  **Default:** `false`

  ```conf
  artwork_individual = <true|false>
  ```

- `artwork_online_sources` - List of online resources for artwork.

  **Notes:**

  - More information regarding artwork can be found [here](artwork.md).

  **Default:** unset

  ```conf
  artwork_online_sources = { "<link-to-source>", "<...>"}
  ```

- `filetypes_ignore` - List of file types ignored by the scanner.

  **Note:** Non-audio files will never be added to the database, but here you can prevent the scanner from even probing them. This might reduce scan time.

  **Default:** `{ ".db", ".ini", ".db-journal", ".pdf", ".metadata" }`

  ```conf
  filetypes_ignore = { "<extension>", "<...>" }
  ```

- `filepath_ignore` - List of paths ignored by the scanner.

  **Note:** If you want to exclude files on a more advanced basis you can enter one or more POSIX regular expressions, and any file with a matching path will be ignored.

  **Default:** unset

  ```conf
  filepath_ignore = { "<path|regular-expression>" }
  ```

- `filescan_disable` - Flag to indicate whether or not the startup file scanning must be disabled.

  **Note:** When OwnTone starts it will do an initial file scan of the library and then watch it for changes. If you are sure your library never changes while OwnTone is not running, you can disable the initial file scan and save some system ressources. Disabling this scan may lead to OwnTone's database coming out of sync with the library. If that happens read the instructions in the README on how to trigger a rescan.

  **Default:** `false`

  ```conf
  filescan_disable = <true|false>
  ```

- `only_first_genre` - Flag to indicate whether or not the first genre only found in metadata must be displayed.

  **Note:** Some tracks have multiple genres separated by semicolon in the same tag, e.g., 'Pop;Rock'. If you don't want them listed like this, you can enable this setting and only the first genre will be used (i.e. 'Pop').

  **Default:** `false`

  ```conf
  only_first_genre = <true|false>
  ```

- `m3u_overrides` - Flag to indicate whether or not the metadata provided by radio streams must be overridden by metadata from m3u playlists, e.g., artist and title in EXTINF.

  **Default:** `false`

  ```conf
  m3u_overrides = <true|false>
  ```

- `itunes_overrides` - Flag to indicate whether or not the library metadata must be overridden by iTunes metadata.

  **Default:** `false`

  ```conf
  itunes_overrides = <true|false>
  ```

- `itunes_smartpl` - Flag to import Should we import the content of iTunes smart playlists.

  **Default:** `false`

  ```conf
  itunes_smartpl = <true|false>
  ```

- `no_decode` - List of formats that are never decoded.

  **Note:** Decoding options for DAAP and RSP clients. Since iTunes has native support for mpeg, mp4a, mp4v, alac and wav, such files will be sent as they are. Any other formats will be decoded to raw wav. If OwnTone detects a non-iTunes DAAP client, it is assumed to only support mpeg and wav, other formats will be decoded. Here you can change when to decode. Note that these settings only affect serving media to DAAP and RSP clients, they have no effect on direct AirPlay, Chromecast and local audio playback

  **Valid values:** `mp4a`, `mp4v`, `mpeg`, `alac`, `flac`, `mpc`, `ogg`, `wma`, `wmal`, `wmav`, `aif`, `wav`.

  **Default:** unset

  ```conf
  no_decode = { "<format>", "<...>" }
  ```

- `force_decode` - List of formats that are always decoded.

  **Note:** See note for `no_decode` setting.

  **Valid values:** `mp4a`, `mp4v`, `mpeg`, `alac`, `flac`, `mpc`, `ogg`, `wma`, `wmal`, `wmav`, `aif`, `wav`.

  **Default:** unset

  ```conf
  force_decode = { "<format>", "<...>" }
  ```

- `prefer_format` - Preferred format to be used.

  **Default:** unset

  ```conf
  prefer_format = "<format>"
  ```

- `decode_audio_filters` - List of audio filters used at decoding time.

  **Note:** These filters are ffmpeg filters: i.e. similar to those specified on the command line `ffmpeg -af <filter>`. Examples: `"volume=replaygain=track"` to use replay gain of the track metadata, or `loudnorm=I=-16:LRA=11:TP=-1.5` to normalise volume.

  **Default:** unset

  ```conf
  decode_audio_filters = { "<filter>" }
  ```

- `decode_video_filters` - List of video filters used at decoding time.

  **Note:** These filters are ffmpeg filters: i.e. similar to those specified on the command line `ffmpeg -vf <filter>`.

  **Default:** unset

  ```conf
  decode_video_filters = { "<filter>" }
  ```

- `pipe_autostart` - Flag to indicate whether or not named pipes must start automatically when data is provided.

  **Note:** To exclude specific pipes from watching, consider using the `filepath_ignore` setting.

  ```conf
  pipe_autostart = true
  ```

- `pipe_sample_rate` - Sampling rate of the pipe.

  **Default:** `44100`.

  ```conf
  pipe_sample_rate = <integer>
  ```

- `pipe_bits_per_sample` - Bits per sample of the pipe.

  **Default:** `16`.

  ```conf
  pipe_bits_per_sample = <integer>
  ```

- `rating_updates` - Flag to indicate whether or not ratings are automatically updated.

  **Note:** When enabled, the rating is automatically updated after a song has either been played or skipped (only skipping to the next song is taken into account). The calculation is taken from the beets plugin "mpdstats" (see [here](https://beets.readthedocs.io/en/latest/plugins/mpdstats.html)). It consists of calculating a stable rating based only on the play and skip count and a rolling rating based on the current rating and the action (played or skipped). Both results are combined with a mix factor of 0.75. **Formula:** new rating = 0.75 × stable rating + 0.25 × rolling rating

  **Default:** `false`

  ```conf
  rating_updates = <true|false>
  ```

- `read_rating` - Flag to indicate whether or not the rating is read from media file metadata.

  **Default:** `false`

  ```conf
  read_rating = <true|false>
  ```

- `write_rating` - Flag to indicate whether or not the rating is written back to the file metadata.

  **Note:** By default, ratings are only saved in the database. To avoid excessive writing to the library, automatic rating updates are not written, even with the write_rating setting enabled.

  **Default:** `false`

  ```conf
  write_rating = <true|false>
  ```

- `max_rating` - Scale used when reading and writing ratings to media files.

  **Default:** `100`

  ```conf
  max_rating = <integer>
  ```

- `allow_modifying_stored_playlists` - Flag to indicate whether or not M3U playlists can be created, modified, or deleted in the playlist directories.

  **Note:** This setting is only supported through the web interface and some MPD clients.

  **Default:** `false`

  ```conf
  allow_modifying_stored_playlists = false
  ```

- `default_playlist_directory` - Name of the directory in one of the library directories that will be used as the default playlist directory.

  **Note:** OwnTone creates new playlists in this directory. This setting requires `allow_modify_stored_playlists` set to true.

  **Default:** unset

  ```conf
  default_playlist_directory = "<path-to-playlist-directory>"
  ```

- `clear_queue_on_stop_disable` - Flag to indicate whether or not the queue is cleared when the playback is stopped.

  **Note:** By default OwnTone will, like iTunes, clear the play queue if playback stops. Setting clear_queue_on_stop_disable to true will keep the playlist like MPD does. Moreover, some dacp clients do not show the play queue if playback is stopped.

  **Default:** `false`

  ```conf
  clear_queue_on_stop_disable = <true|false>
  ```

## Local Audio Settings

```conf
audio {
  ...
}
```

The `audio` section is meant to configure the local audio output. It accepts the settings below.

- `nickname` - Name appearing in the speaker list.

  **Default:** `"Computer"`

  ```conf
  nickname = "Computer"
  ```

- `type` - Type of the output.

  **Valid values:** `alsa`, `pulseaudio`, `dummy`, `disabled`

  **Default:** unset

  ```conf
  type = "<alsa|pulseaudio|dummy|disabled>"
  ```

- `server` - For pulseaudio output, an optional server hostname or IP can be specified (e.g. "localhost"). If not set, connection is made via local socket.

  **Default:** unset

  ```conf
  server = "<hostname|ip-address>"
  ```

- `card` - Name of the local audio PCM device.

  **Note:** ALSA only.

  **Default:** `"default"`

  ```conf
  card = "<device-name>"
  ```

- `mixer` - Mixer channel used for volume control.

  **Note:** Usable with ALSA only. If not set, PCM will be used if available, otherwise Master.

  **Default:** unset

  ```conf
  mixer = "<mixer>"
  ```

- `mixer_device` - Name of the mixer device to use for volume control.

  **Note:** Usable with ALSA only.

  **Default:** unset

  ```conf
  mixer_device = "<mixer-device>"
  ```

- `sync_disable` - Flag to indicate whether or not audio resampling has to be enabled to keep local audio in sync with, for example, Airplay.

  **Note:** This feature relies on accurate ALSA measurements of delay, and some devices don't provide that. If that is the case you are better off disabling the feature.

  **Default:** `false`

  ```conf
  sync_disable = <true|false>
  ```

- `offset_ms` - Start delay in milliseconds relatively to other speakers, for example Airplay.

  **Note:** Negative values correspond to moving local audio ahead, positive correspond to delaying it.

  **Valid values:** -1000 to 1000

  **Default:** `0`

  ```conf
  offset_ms = 0
  ```

- `adjust_period_seconds` - Period in seconds used to collect measurements for drift and latency adjustments.

  **Note:** To calculate if resampling is required and if yes what value, local audio delay is measured each second. After a period the collected measurements are used to estimate drift and latency, which determines if corrections are required.

  **Default:** `100`

  ```conf
  adjust_period_seconds = <integer>
  ```

## Per ALSA Device Settings

```conf
alsa "<card-name>" {

  }
```

Each `alsa` section is meant to configure a named ALSA output: one named section per device. It accepts the settings below.

**Note:** Make sure to set the `"<card-name>"` correctly. Moreover, these settings will override the ALSA settings in the `audio` section above.

- `nickname` - Name appearing in the speaker list.

  **Default:** `"<card-name>"`

  ```conf
  nickname = "<speaker-name>"
  ```

- `mixer` - Mixer channel used for volume control.

  **Note:** If not set, PCM will be used if available, otherwise Master.

  ```conf
  mixer = "<mixer>"
  ```

- `mixer_device` - Name of the mixer device to use for volume control.

  **Default:** `"<card-name>"`

  ```conf
  mixer_device = "<mixer-device>"
  ```

- `offset_ms` - Start delay in milliseconds relatively to other speakers, for example Airplay.

  **Note:** Negative values correspond to moving local audio ahead, positive correspond to delaying it.

  **Valid values:** -1000 to 1000

  **Default:** `0`

  ```conf
  offset_ms = 0
  ```

## FIFO Settings

```conf
fifo {
  ...
}
```

The `fifo` section, is meant to configure the named pipe audio output. It accepts the settings below.

- `nickname` - The name appearing in the speaker list.

  **Default:** `"fifo"`

  ```conf
  nickname = "<fifo-name>"
  ```

- `path` - Path to the named pipe.

  **Default:** unset

  ```conf
  path = "<path-to-fifo>"
  ```

## Shared AirPlay Settings

```conf
airplay_shared {
  ...
}
```

The `airplay_shared` section describes the settings that are shared across all the AirPlay devices.

- `control_port` - Number of the UDP control port used when Airplay devices make connections back to OwnTone.

  **Note:** Choosing specific ports may be helpful when running OwnTone behind a firewall.

  **Default:** `0`

  ```conf
  control_port = 0
  ```

- `timing_port` - Number of the UDP timing port used when Airplay devices make connections back to OwnTone.

  **Note:** Choosing specific ports may be helpful when running OwnTone behind a firewall.

  **Default:** `0`

  ```conf
  timing_port = 0
  ```

- `uncompressed_alac` - Switch Airplay 1 streams to uncompressed ALAC (as opposed to regular, compressed ALAC). Reduces CPU use at the cost of network bandwidth.

  **Default:** `false`

  ```conf
  uncompressed_alac = <true|false>
  ```

## Per AirPlay Device Settings

```conf
airplay "<airplay-device>" {

}
```

Each `airplay` section is meant to configure a named Airplay output: one named section per device. It accepts the settings below.

**Note:** The capitalisation of the device name is relevant.

- `max_volume` - Maximum value of the volume.

  **Note:** If that's more than your setup can handle set a lower value.

  **Default:** `11`

  ```conf
  max_volume = <integer>
  ```

- `exclude` - Flag indicating if the device must be excluded from the speaker list.

  **Default:** `false`

  ```conf
  exclude = <true|false>
  ```

- `permanent` - Flag to indicate to keep the device in the speaker list and thus ignore mdns notifications about it no longer being present. The speaker will remain until restart of OwnTone.

  **Default:** `false`

  ```conf
  permanent = <true|false>
  ```

- `reconnect` - Flag to indicate whether or not OwnTone must explicitly reconnect with the device.

  **Note:** Some devices spuriously disconnect during playback, and based on the device type OwnTone may attempt to reconnect.

  **Default:** `false`

  ```conf
  reconnect = <true|false>
  ```

- `password`- Password of the device.

  **Default:** unset

  ```conf
  password = "<password>"
  ```

- `raop_disable` - Flag to indicate whether or not AirPlay 1 (RAOP) must be disabled.

  **Default:** `false`

  ```conf
  raop_disable = <true|false>
  ```

- `nickname` - Name appearing in the speaker list.

  **Note:** The defined name overrides the name of the device.

  **Default:** unset

  ```conf
  nickname = "<speaker-name>"
  ```

## Per Chromecast Device Settings

```conf
chromecast "<chromecast-device>" {
  ...
}
```

Each `chromecast` section is meant to configure a named Chromecast output: one named section per device. It accepts the settings below.

**Note:** The capitalisation of the device name is relevant.

- `max_volume` - Maximum value of the volume.

  **Note:** If that's more than your setup can handle set a lower value.

  **Default:** `11`

  ```conf
  max_volume = <integer>
  ```

- `exclude` - Flag indicating if the device must be excluded from the speaker list.

  **Default:** `false`

  ```conf
  exclude = <true|false>
  ```

- `nickname` - Name appearing in the speaker list.

  **Note:** The defined name overrides the name of the device.

  **Default:** unset

  ```conf
  nickname = "<speaker-name>"
  ```

## Spotify Settings

```conf
spotify {
  ...
}
```

The `spotify` section accepts the settings below.

**Note:** These settings only have effect if OwnTone is built with Spotify support.

- `bitrate` - Bit rate of the stream.

  **Valid values:** `0` (No preference), `1` (96 kb/s), `2` (160 kb/s), `3` (320 kb/s)

  **Default:** `0`

  ```conf
  bitrate = <0|1|2|3>
  ```

- `base_playlist_disable` - Flag to indicate whether or not Spotify playlists are placed into the library playlist folder.

  **Note:** Spotify playlists are by default located in a _Spotify_ playlist folder.

  **Default:** `false`

  ```conf
  base_playlist_disable = <true|false>
  ```

- `artist_override` - Flag indicating whether or not the compilation artist must be used as the album artist.

  **Note:** Spotify playlists usually have many artists, and if you don't want every artist to be listed when artist browsing in Remote, you can set this flag to true.

  **Default:** `false`

  ```conf
  artist_override = <true|false>
  ```

- `album_override` - Flag to indicate to use the playlist name as the album name.

  **Note:** Similar to the different artists in Spotify playlists, the playlist items belong to different albums, and if you do not want every album to be listed when browsing in Remote, you can set the album_override flag to true. Moreover, if an item is in more than one playlist, it will only appear randomly in one album when browsing.

  **Default:** `false`

  ```conf
  album_override = <true|false>
  ```

## RCP / Roku Soundbridge Settings

```conf
rcp "<device-name>" {

}
```

Each `rcp` section is meant to configure a named RCP output: one named section per device. It accepts the settings below.

**Note:** The capitalisation of the device name is relevant.

- `exclude` - Enable this option to exclude the device from the speaker list.

  **Default:** `false`

  ```conf
  exclude = <true|false>
  ```

- `clear_on_close` - Flag indicating whether or not the power on the device is maintained.

  **Note:** A Roku / SoundBridge can power up in 2 modes: (default) reconnect to the previously used library (i.e. OwnTone) or in a _cleared library_ mode. The Roku power up behaviour is affected by how OwnTone disconnects from the Roku device. Set to false to maintain the default Roku _power on_ behaviour.

  **Default:** `false`

  ```conf
  clear_on_close = false
  ```

## MPD Settings

```conf
mpd {
  ...
}
```

The `mpd` section defines the settings for MPD clients. It accepts the settings below.

**Note:** These settings only have effect if OwnTone is built with Spotify support.

- `port` - TCP port to listen for MPD client requests.

  **Note:** Setting the port to `0` disables the support for MPD.

  **Default:** `6600`

  ```conf
  port = 6600
  ```

- `http_port` - HTTP port to listen for artwork requests.

  **Notes:**

  - This setting is only supported by some MPD clients and will need additional configuration in the MPD client to work.
  - Setting the port to `0` disables the serving of artwork.

  **Default:** `0`

  ```conf
  http_port = 0
  ```

## SQLite Settings

```conf
sqlite {
  ...
}
```

The `sqlite` section defines how the SQLite database operates and accepts the settings below.

**Note:** Make sure to read the SQLite documentation for the corresponding PRAGMA statements as changing them from the defaults may increase the possibility of database corruption. By default, the SQLite default values are used.

- `pragma_cache_size_library` - Cache size in number of database pages for the library database.

  **Note:** SQLite default page size is 1024 bytes and cache size is 2000 pages.

  ```conf
  pragma_cache_size_library = <pages>
  ```

- `pragma_cache_size_cache` - Cache size in number of db pages for the cache database.

  **Note:** SQLite default page size is 1024 bytes and cache size is 2000 pages.

  ```conf
  pragma_cache_size_cache = <pages>
  ```

- `pragma_journal_mode` - Sets the journal mode for the database. Valid values are: `DELETE`, `TRUNCATE`, `PERSIST`, `MEMORY`, `WAL`, and `OFF`.

  **Default:** `"DELETE"`

  ```conf
  pragma_journal_mode = "<DELETE|TRUNCATE|PERSIST|MEMORY|WAL|OFF>"
  ```

- `pragma_synchronous` - Change the setting of the "synchronous" flag.

  **Valid values:** `0` (off), `1` (normal), `2` (full)

  **Default:** `2`

  ```conf
  pragma_synchronous = <0|1|2>
  ```

- `pragma_mmap_size_library` - Number of bytes set aside for memory-mapped I/O for the library database.

  **Notes:** This setting requires SQLite 3.7.17+.

  **Valid values:** `0` (mmap disabled), `<integer>` (bytes for mmap)

  **Default:** `0`

  ```conf
  pragma_mmap_size_library = <integer>
  ```

- `pragma_mmap_size_cache` - Number of bytes set aside for memory-mapped I/O for the cache database.

  **Note:** This setting requires SQLite 3.7.17+.

  **Valid values:** `0` (mmap disabled), `<integer>` (bytes for mmap)

  ```conf
  pragma_mmap_size_cache = <integer>
  ```

- `vacuum` - Flag indicating whether or not the database must be vacuumed on startup.

  **Note:** This setting increases the startup time, but may reduce database size.

  **Default:** `true`

  ```conf
  vacuum = <true|false>
  ```

## Streaming Settings

```conf
streaming {
  ...
}
```

The `streaming` section defines the audio settings for the streaming URL (`http://<server-name>:<port-number>/stream.mp3`) and accepts the settings below.

- `sample_rate` - Sampling rate of the stream: e.g., 44100, 48000, etc.

  **Default:** `44100`

  ```conf
  sample_rate = <integer>
  ```

- `bit_rate` - Bit rate of the stream (in kb/s).

  **Valid values:** `64`, `96`, `128`, `192`, and `320`.

  **Default:** `192`

  ```conf
  bit_rate = <64|96|128|192|320>
  ```

- `icy_metaint` - Number of bytes of media stream data between each metadata chunk.

  **Default:** `16384`

  ```conf
  icy_metaint = <integer>
  ```
