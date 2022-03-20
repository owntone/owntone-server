# Playlists and internet radio

OwnTone supports M3U and PLS playlists. Just drop your playlist somewhere
in your library with an .m3u or .pls extension and it will pick it up.

From the web interface, and some mpd clients, you can also create and modify
playlists by saving the current queue. Click the "Save" button. Note that this
requires that `allow_modifying_stored_playlists` is enabled in the configuration
file, and that the server has write access to `default_playlist_directory`.

If the playlist contains an http URL it will be added as an internet radio
station, and the URL will be probed for Shoutcast (ICY) metadata. If the radio
station provides artwork, OwnTone will download it during playback and send
it to any remotes or AirPlay devices requesting it.

Instead of downloading M3U's from your radio stations, you can also make an
empty M3U file and insert links in it to the M3U's of your radio stations.

Radio streams can only be played by OwnTone, so that means they will not be
available to play in DAAP clients like iTunes.

The server can import playlists from iTunes Music Library XML files. By default,
metadata from our parsers is preferred over what's in the iTunes DB; use
itunes_overrides = true if you prefer iTunes' metadata.

OwnTone has support for smart playlists. How to create a smart playlist is
documented in [Smart playlists](smart-playlists.md).

If you're not satisfied with internet radio metadata that OwnTone shows,
then you can read about tweaking it in
[Radio streams](advanced/radio-streams.md).
