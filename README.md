# forked-daapd

forked-daapd is a Linux/FreeBSD DAAP (iTunes), MPD (Music Player Daemon) and
RSP (Roku) media server.

It supports AirPlay devices/speakers, Apple Remote (and compatibles),
MPD clients, Chromecast, network streaming, internet radio, Spotify and LastFM.

It does not support streaming video by AirPlay nor Chromecast.

DAAP stands for Digital Audio Access Protocol which is the protocol used
by iTunes and friends to share/stream media libraries over the network.

forked-daapd is a complete rewrite of mt-daapd (Firefly Media Server).


## Looking for help?

Before you continue, make sure you know what version of forked-daapd you have,
and what features it was built with (e.g. Spotify support).

How to find out? Go to the [web interface](http://forked-daapd.local:3689) and
check. No web interface? Then check the top of forked-daapd's log file (usually
/var/log/forked-daapd.log).

Note that you are viewing a snapshot of the instructions that may or may not
match the version of forked-daapd that you are using. Go to
[references](#references) to find instructions for previous versions of
forked-daapd.

If you are looking for help on building forked-daapd (not using it), then
please see the
[INSTALL](https://github.com/ejurgensen/forked-daapd/blob/master/INSTALL) file.


## Contents

- [Getting started](#getting-started)
- [Supported clients](#supported-clients)
- [Web interface](#web-interface)
- [Using Remote](#using-remote)
- [AirPlay devices/speakers](#airplay-devicesspeakers)
- [Chromecast](#chromecast)
- [Local audio through ALSA](#local-audio-through-alsa)
- [Local audio, Bluetooth and more through Pulseaudio](#local-audio-bluetooth-and-more-through-pulseaudio)
- [MP3 network streaming (streaming to iOS)](#mp3-network-streaming-streaming-to-ios)
- [Remote access](#remote-access)
- [Supported formats](#supported-formats)
- [Playlists and internet radio](#playlists-and-internet-radio)
- [Artwork](#artwork)
- [Library](#library)
- [Command line](#command-line)
- [Spotify](#spotify)
- [LastFM](#lastfm)
- [MPD clients](#mpd-clients)
- [References](#references)


## Getting started

After installation (see [INSTALL](https://github.com/ejurgensen/forked-daapd/blob/master/INSTALL))
do the following:

 1. Edit the configuration file (usually `/etc/forked-daapd.conf`) to suit your
    needs
 2. Start or restart the server (usually `/etc/init.d/forked-daapd restart`)
 3. Go to the web interface [http://forked-daapd.local:3689](http://forked-daapd.local:3689),
    or, if that won't work, to [http://[your_server_address_here]:3689](http://[your_server_address_here]:3689)
 4. Wait for the library scan to complete
 5. If you will be using a remote, e.g. Apple Remote: Start the remote, go to
    Settings, Add Library
 6. Enter the pair code in the web interface (update the page with F5 if it does
    not automatically pick up the pairing request)


## Supported clients

forked-daapd supports these kinds of clients:

- DAAP clients, like iTunes or Rhythmbox
- Remote clients, like Apple Remote or compatibles for Android/Windows Phone
- AirPlay devices, like AirPort Express, Shairport and various AirPlay speakers
- Chromecast devices
- MPD clients, like mpc (see [mpd-clients](#mpd-clients))
- MP3 network stream clients, like VLC and almost any other music player
- RSP clients, like Roku Soundbridge

Like iTunes, you can control forked-daapd with Remote and stream your music
to AirPlay devices.

A single forked-daapd instance can handle several clients concurrently,
regardless of the protocol.

By default all clients on 192.168.* (and the ipv6 equivalent) are allowed to
connect without authentication. You can change that in the configuration file.

Here is a list of working and non-working DAAP and Remote clients. The list is
probably obsolete when you read it :-)

|          Client          | Developer   |  Type  |   Platform    | Working (vers.) |
| ------------------------ | ----------- | ------ | ------------- | --------------- |
| iTunes                   | Apple       | DAAP   | Win, OSX      | Yes (12.7)      |
| Rhythmbox                | Gnome       | DAAP   | Linux         | Yes             |
| Diapente                 | diapente    | DAAP   | Android       | Yes             |
| WinAmp DAAPClient        | WardFamily  | DAAP   | WinAmp        | Yes             |
| Amarok w/DAAP plugin     | KDE         | DAAP   | Linux/Win     | Yes (2.8.0)     |
| Banshee                  |             | DAAP   | Linux/Win/OSX | No (2.6.2)      |
| jtunes4                  |             | DAAP   | Java          | No              |
| Firefly Client           |             | (DAAP) | Java          | No              |
| Remote                   | Apple       | Remote | iOS           | Yes (4.3)       |
| Retune                   | SquallyDoc  | Remote | Android       | Yes (3.5.23)    |
| TunesRemote+             | Melloware   | Remote | Android       | Yes (2.5.3)     |
| Remote for iTunes        | Hyperfine   | Remote | Android       | Yes             |
| Remote for Windows Phone | Komodex     | Remote | Windows Phone | Yes (2.2.1.0)   |
| TunesRemote SE           |             | Remote | Java          | Yes (r108)      |
| rtRemote for Windows     | bizmodeller | Remote | Windows       | Yes (1.2.0.67)  |


## Web interface

You can find forked-daapd's web interface at [http://forked-daapd.local:3689](http://forked-daapd.local:3689)
or alternatively at [http://[your_server_address_here]:3689](http://[your_server_address_here]:3689).

Use the web interface to control playback, trigger manual library rescans, pair
with remotes, select speakers, authenticate with Spotify, etc.

You can find some screenshots and build instructions in [README_PLAYER_WEBINTERFACE.md](https://github.com/ejurgensen/forked-daapd/blob/master/README_PLAYER_WEBINTERFACE.md).

## Using Remote

Remote gets a list of output devices from the server; this list includes any
and all devices on the network we know of that advertise AirPlay: AirPort
Express, Apple TV, ... It also includes the local audio output, that is, the
sound card on the server (even if there is no soundcard).

If no output is selected when playback starts, forked-daapd will try to
autoselect a device.

forked-daapd remembers your selection and the individual volume for each
output device; selected devices will be automatically re-selected, except if
they return online during playback.

### Pairing

 1. Open the [web interface](http://forked-daapd.local:3689)
 2. Start Remote, go to Settings, Add Library
 3. Enter the pair code in the web interface (update the page with F5 if it does
    not automatically pick up the pairing request)

If Remote doesn't connect to forked-daapd after you entered the pairing code
something went wrong. Check the log file to see the error message. Here are
some common reasons:

#### You did not enter the correct pairing code
You will see an error in the log about pairing failure with a HTTP response code
that is *not* 0.
Solution: Try again.

#### No response from Remote, possibly a network issue
If you see an error in the log with either:
 - a HTTP response code that is 0
 - "Empty pairing request callback"
it means that forked-daapd could not establish a connection to Remote. This 
might be a network issue, your router may not be allowing multicast between the
Remote device and the host forked-daapd is running on.
Solution 1: Sometimes it resolves the issue if you force Remote to quit, restart
it and do the pairing proces again. Another trick is to establish some other
connection (eg SSH) from the iPod/iPhone/iPad to the host.
Solution 2: Check your router settings if you can whitelist multicast addresses
under IGMP settings. For Apple Bonjour, setting a multicast address of
224.0.0.251 and a netmask of 255.255.255.255 should work.

Otherwise try using avahi-browse for troubleshooting:
 - in a terminal, run `avahi-browse -r -k _touch-remote._tcp`
 - start Remote, goto Settings, Add Library
 - after a couple seconds at most, you should get something similar to this:

```
+ ath0 IPv4 59eff13ea2f98dbbef6c162f9df71b784a3ef9a3      _touch-remote._tcp   local
= ath0 IPv4 59eff13ea2f98dbbef6c162f9df71b784a3ef9a3      _touch-remote._tcp   local
   hostname = [Foobar.local]
   address = [192.168.1.1]
   port = [49160]
   txt = ["DvTy=iPod touch" "RemN=Remote" "txtvers=1" "RemV=10000" "Pair=FAEA410630AEC05E" "DvNm=Foobar"]
```

Hit Ctrl-C to terminate avahi-browse.

To check for network issues you can try to connect to address and port with
telnet.


## AirPlay devices/speakers

forked-daapd will discover the AirPlay devices available on your network. For
devices that are password-protected, the device's AirPlay name and password
must be given in the configuration file. See the sample configuration file
for the syntax.

If your Apple TV requires device verification (always required by Apple TV4 with
tvOS 10.2) then you can do that through the web interface: Select the device and
then enter the PIN that the Apple TV displays.

For troubleshooting, see [using Remote](#using-remote).


## Chromecast

forked-daapd will discover Chromecast devices available on your network. There
is no configuration to be done. This feature relies on streaming the audio in
mp3 to your Chromecast device, which means that mp3 encoding must be supported
by your ffmpeg/libav. See [MP3 network streaming](#mp3-network-streaming-streaming-to-ios).


## Local audio through ALSA

In the config file, you can select ALSA for local audio. This is the default.

When using ALSA, the server will try to syncronize playback with AirPlay. You
can adjust the syncronization in the config file.


## Local audio, Bluetooth and more through Pulseaudio

In the config file, you can select Pulseaudio for local audio. In addition to
local audio, Pulseaudio also supports an array of other targets, e.g. Bluetooth
or DLNA. However, Pulseaudio does require some setup, so here is a separate page
with some help on that: [README_PULSE.md](https://github.com/ejurgensen/forked-daapd/blob/master/README_PULSE.md)

Note that if you select Pulseaudio the "card" setting in the config file has
no effect. Instead all soundcards detected by Pulseaudio will be listed as
speakers by forked-daapd.

You can adjust the latency of Pulseaudio playback in the config file.


## MP3 network streaming (streaming to iOS)

You can listen to audio being played by forked-daapd by opening this network
stream address in pretty much any music player:

 http://[your hostname/ip address]:3689/stream.mp3

This is currently the only way of listening to your audio on iOS devices, since
Apple does not allow AirPlay receiver apps, and because Apple Home Sharing
cannot be supported by forked-daapd. So what you can do instead is install a
music player app like VLC, connect to the stream and control playback with
Remote. You can also use MPoD in "On the go"-mode, where control and playback is
integrated in one app (see (#mpd-clients)).

Note that MP3 encoding must be supported by ffmpeg/libav for this to work. If
it is not available you will see a message in the log file. In Debian/Ubuntu you
get MP3 encoding support by installing the package "libavcodec-extra".


## Remote access

It is possible to access a shared library over the internet from a DAAP client
like iTunes. You must have remote access to the host machine.

First log in to the host and forward port 3689 to your local machine. You now
need to broadcast the daap service to iTunes on your local machine. On macOS the
command is:

```
dns-sd -P iTunesServer _daap._tcp local 3689 localhost.local 127.0.0.1 "ffid=12345"
```

The `ffid` key is required but its value does not matter.

Your library will now appear as 'iTunesServer' in iTunes.


## Supported formats

forked-daapd should support pretty much all audio formats. It relies on libav
(or ffmpeg) to extract metadata and decode the files on the fly when the client
doesn't support the format.

Formats are attributed a code, so any new format will need to be explicitely
added. Currently supported:
 - MPEG4: mp4a, mp4v
 - AAC: alac
 - MP3 (and friends): mpeg
 - FLAC: flac
 - OGG VORBIS: ogg
 - Musepack: mpc
 - WMA: wma (WMA Pro), wmal (WMA Lossless), wmav (WMA video)
 - AIFF: aif
 - WAV: wav
 - Monkey's audio: ape


## Playlists and internet radio

forked-daapd supports M3U and PLS playlists. Just drop your playlist somewhere
in your library with an .m3u or .pls extension and it will pick it up.

If the playlist contains an http URL it will be added as an internet radio
station, and the URL will be probed for Shoutcast (ICY) metadata. If the radio
station provides artwork, forked-daapd will download it during playback and send
it to any remotes or AirPlay devices requesting it.

Instead of downloading M3U's from your radio stations, you can also make an
empty M3U file and in it insert links to the M3U's of your radio stations.

Support for iTunes Music Library XML format is available as a compile-time
option. By default, metadata from our parsers is preferred over what's in
the iTunes DB; use itunes_overrides = true if you prefer iTunes' metadata.

forked-daapd has support for smart playlists. How to create a smart playlist is
documented in
[README_SMARTPL.md](https://github.com/ejurgensen/forked-daapd/blob/master/README_SMARTPL.md).


## Artwork

forked-daapd has support for PNG and JPEG artwork which is either:
 - embedded in the media files
 - placed as separate image files in the library
 - made available online by the radio station

For media in your library, forked-daapd will try to locate album and artist
artwork (group artwork) by the following procedure:
 - if a file {artwork,cover,Folder}.{png,jpg} is found in one of the directories
   containing files that are part of the group, it is used as the artwork. The
   first file found is used, ordering is not guaranteed;
 - failing that, if [directory name].{png,jpg} is found in one of the
   directories containing files that are part of the group, it is used as the
   artwork. The first file found is used, ordering is not guaranteed;
 - failing that, individual files are examined and the first file found 
   with an embedded artwork is used. Here again, ordering is not guaranteed.

{artwork,cover,Folder} are the default, you can add other base names in the
configuration file. Here you can also enable/disable support for individual
file artwork (instead of using the same artwork for all tracks in an entire
album).

For playlists in your library, say /foo/bar.m3u, then for any http streams in
the list, forked-daapd will look for /foo/bar.{jpg,png}.

You can use symlinks for the artwork files.

forked-daapd caches artwork in a separate cache file. The default path is 
`/var/cache/forked-daapd/cache.db` and can be configured in the configuration 
file. The cache.db file can be deleted without losing the library and pairing 
informations.


## Library

The library is scanned in bulk mode at startup, but the server will be available
even while this scan is in progress. You can follow the progress of the scan in
the log file or via the web interface. When the scan is complete you will see
the log message: "Bulk library scan completed in X sec".

The very first scan will take longer than subsequent startup scans, since every
file gets analyzed. At the following startups the server looks for changed files
and only analyzis those.

Updates to the library are reflected in real time after the initial scan, so you
do not need to manually start rescans. The directories are monitored for changes
and rescanned on the fly. Note that if you have your library on a network mount
then real time updating may not work. Read below about what to do in that case.

If you change any of the directory settings in the library section of the
configuration file a rescan is required before the new setting will take effect.
You can do this by using "Update library" from the web interface.

Symlinks are supported and dereferenced, but it is best to use them for
directories only.


### Pipes (for e.g. multiroom with Shairport-sync)

Some programs, like for instance Shairport-sync, can be configured to output
audio to a named pipe. If this pipe is placed in the library, forked-daapd will
automatically detect that it is there, and when there is audio being written to
it, playback of the audio will be autostarted (and stopped).

Using this feature, forked-daapd can act as an AirPlay multiroom "router": You
can have an AirPlay source (e.g. your iPhone) send audio Shairport-sync, which
forwards it to forked-daapd through the pipe, which then plays it on whatever
speakers you have selected (through Remote).

The format of the audio being written to the pipe must be PCM16.

You can also start playback of pipes manually. You will find them in remotes 
listed under "Unknown artist" and "Unknown album". The track title will be the
name of the pipe.

Shairport-sync can write metadata to a pipe, and forked-daapd can read this.
This requires that the metadata pipe has the same filename as the audio pipe
plus a ".metadata" suffix. Say Shairport-sync is configured to write audio to
"/foo/bar/pipe", then the metadata pipe should be "/foo/bar/pipe.metadata".


### Libraries on network mounts

Most network filesharing protocols do not offer notifications when the library
is changed. So that means forked-daapd cannot update its database in real time.
Instead you can schedule a cron job to update the database.

The first step in doing this is to add two entries to the 'directories'
configuration item in forked-daapd.conf:

```
  directories = { "/some/local/dir", "/your/network/mount/library" }
```

Now you can make a cron job that runs this command:

```
  touch /some/local/dir/trigger.init-rescan
```

When forked-daapd detects a file with filename ending .init-rescan it will
perform a bulk scan similar to the startup scan.


### Troubleshooting library issues

If you place a file with the filename ending .full-rescan in your library,
you can trigger a full rescan of your library. This will clear all music and
playlists from forked-daapd's database and initiate a fresh bulk scan. Pairing
and speaker information will be kept. Only use this for troubleshooting, it is
not necessary during normal operation.


## Command line

You can choose between:

- a [MPD command line client](#mpd-clients) (easiest) like `mpc`
- curl with forked-daapd's JSON API (see [README_JSON_API.md](https://github.com/ejurgensen/forked-daapd/blob/master/README_JSON_API.md))
- curl with DAAP/DACP commands (hardest)

Here is an example of how to use curl with DAAP/DACP. Say you have a playlist
with a radio station, and you want to make a script that starts playback of that
station:

1. Run `sqlite3 [your forked-daapd db]`. Use `select id,title from files` to get
   the id of the radio station, and use `select id,title from playlists` to get
   the id of the playlist.
2. Convert the two ids to hex.
3. Put the following lines in the script with the relevant ids inserted (also
   observe that you must use a session-id < 100, and that you must login and
   logout):

```
curl "http://localhost:3689/login?pairing-guid=0x1&request-session-id=50"
curl "http://localhost:3689/ctrl-int/1/playspec?database-spec='dmap.persistentid:0x1'&container-spec='dmap.persistentid:0x[PLAYLIST-ID]'&container-item-spec='dmap.containeritemid:0x[FILE ID]'&session-id=50"
curl "http://localhost:3689/logout?session-id=50"
```


## Spotify

forked-daapd has support for playback of the tracks in your Spotify library.

1. Go to the [web interface](http://forked-daapd.local:3689) and check that your
   version of forked-daapd was built with Spotify support.
2. You must have a Spotify premium account. If you normally log into Spotify
   with your Facebook account you must first go to Spotify's web site where you
   can get the Spotify username and password that matches your account.
3. Make sure you have `libspotify` installed. Unfortunately, it is no longer
   available from Spotify, and at the time of writing this they have not
   provided an alternative. However, on most Debian-based platforms, you can
   still get it like this:
   - Add the mopidy repository, see [instructions](https://apt.mopidy.com)
   - Install with `apt install libspotify-dev`

Once the above is in order you can login to Spotify via the web interface. The
procedure for logging in to Spotify is a two-step procedure due to the current
state of libspotify, but the web interface makes both steps available to you.

Spotify will automatically notify forked-daapd about playlist updates, so you
should not need to restart forked-daapd to syncronize with Spotify. However,
Spotify only notifies about playlist updates, not new saved tracks/albums, so
you need to manually trigger a rescan to get those.

forked-daapd will not store your password, but will still be able to log you in
automatically afterwards, because libspotify saves a login token. You can
configure the location of your Spotify user data in the configuration file.

To permanently logout and remove credentials, delete the contents of
`/var/cache/forked-daapd/libspotify` (while forked-daapd is stopped).

Limitations:
You will not be able to do any playlist management through forked-daapd - use
a Spotify client for that. You also can only listen to your music by letting
forked-daapd do the playback - so that means you can't stream from forked-daapd
to iTunes.


## LastFM

You can have forked-daapd scrobble the music you listen to. To set up scrobbling
go to the web interface and authorize forked-daapd with your LastFM credentials.

forked-daapd will not store your LastFM username/password, only the session key.
The session key does not expire.


## MPD clients

You can - to some extent - use clients for MPD to control forked-daapd.

By default forked-daapd listens on port 6600 for MPD clients. You can change
this in the configuration file.

Currently only a subset of the commands offered by MPD (see [MPD protocol documentation](http://www.musicpd.org/doc/protocol/)) 
are supported by forked-daapd.

Due to some differences between forked-daapd and MPD not all commands will act
the same way they would running MPD:

- crossfade, mixrampdb, mixrampdelay and replaygain will have no effect
- single, repeat: unlike MPD forked-daapd does not support setting single and repeat separately 
  on/off, instead repeat off, repeat all and repeat single are supported. Thus setting single on 
  will result in repeat single, repeat on results in repeat all.

The following table shows what is working for a selection of MPD clients:

|          Client                               |  Type  | Status          |
| --------------------------------------------- | ------ | --------------- |
| [mpc](http://www.musicpd.org/clients/mpc/)    | CLI    | Working commands: mpc, add, crop, current, del (ranges are not yet supported), play, next, prev (behaves like cdprev), pause, toggle, cdprev, seek, clear, outputs, enable, disable, playlist, ls, load, volume, repeat, random, single, search, find, list, update (initiates an init-rescan, the path argument is not supported)   |
| [ympd](http://www.ympd.org/)                  | Web    | Everything except "add stream" should work |


## References

The source for this version of forked-daapd can be found here:

  [https://github.com/ejurgensen/forked-daapd.git](https://github.com/ejurgensen/forked-daapd.git)

The original (now unmaintained) source can be found here:

  [http://git.debian.org/?p=users/jblache/forked-daapd.git](http://git.debian.org/?p=users/jblache/forked-daapd.git)

README's for previous versions of forked-daapd:

  [forked-daapd version 26.4](https://github.com/ejurgensen/forked-daapd/blob/26.4/README.md)

  [forked-daapd version 26.3](https://github.com/ejurgensen/forked-daapd/blob/26.3/README.md)

  [forked-daapd version 26.2](https://github.com/ejurgensen/forked-daapd/blob/26.2/README.md)

  [forked-daapd version 26.1](https://github.com/ejurgensen/forked-daapd/blob/26.1/README.md)

  [forked-daapd version 26.0](https://github.com/ejurgensen/forked-daapd/blob/26.0/README.md)

  [forked-daapd version 25.0](https://github.com/ejurgensen/forked-daapd/blob/25.0/README.md)

  [forked-daapd version 24.2](https://github.com/ejurgensen/forked-daapd/blob/24.2/README.md)

  [forked-daapd version 24.1](https://github.com/ejurgensen/forked-daapd/blob/24.1/README.md)
