# Playlists and internet radio

OwnTone supports M3U and PLS playlists. Just drop your playlist somewhere
in your library with an .m3u or .pls extension and it will pick it up.

## Internet Radio

If the playlist contains an http URL it will be added as an internet radio
station, and the URL will be probed for Shoutcast (ICY) metadata. If the radio
station provides artwork, OwnTone will download it during playback and send
it to any remotes or AirPlay devices requesting it.

Instead of downloading M3U's from your radio stations, you can also make an
empty M3U file and in it insert links to the M3U's of your radio stations.

Radio streams can only be played by OwnTone, so that means they will not be
available to play in DAAP clients like iTunes.

If you're not satisfied with internet radio metadata that OwnTone shows,
then you can read about tweaking it under
[Advanced Topics / Radio Streams](streams.md).

## Smart Playlists

OwnTone has support for smart playlists. How to create a smart playlist is
documented in [Smart Playlists](smartpl.md).
