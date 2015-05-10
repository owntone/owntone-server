# forked-daapd

forked-daapd is a Linux/FreeBSD DAAP (iTunes) and RSP (Roku) media server.

It has support for AirPlay devices/speakers, Apple Remote (and compatibles),
internet radio, Spotify and LastFM. It does not support AirPlay video.

DAAP stands for Digital Audio Access Protocol, and is the protocol used
by iTunes and friends to share/stream media libraries over the network.

RSP is Roku's own media sharing protocol. Roku are the makers of the
SoundBridge devices. See http://www.roku.com.

The source for this version of forked-daapd can be found here:

  https://github.com/ejurgensen/forked-daapd.git

The original (now unmaintained) source can be found here:

  http://git.debian.org/?p=users/jblache/forked-daapd.git

forked-daapd is a complete rewrite of mt-daapd (Firefly Media Server).


## Contents of this README

- [Getting started](#getting-started)
- [Supported clients](#supported-clients)
- [Using Remote](#using-remote)
- [AirPlay devices/speakers](#airplay-devicesspeakers)
- [Local audio output](#local-audio-output)
- [Supported formats](#supported-formats)
- [Streaming MPEG4](#streaming-mpeg4)
- [Playlists and internet radio](#playlists-and-internet-radio)
- [Artwork](#artwork)
- [Library](#library)
- [Command line and web interface](#command-line-and-web-interface)
- [Spotify](#spotify)
- [LastFM](#lastfm)
- [MPD clients](#mpd-clients)


## Getting started

After installation (see [INSTALL](INSTALL)) do the following:

 1. Edit the configuration file (usually `/etc/forked-daapd.conf`) to suit your
    needs
 2. Start or restart the server (usually `/etc/init.d/forked-daapd restart`)
 3. Wait for the library scan to complete. You can follow the progress with
    `tail -f /var/log/forked-daapd.log`
 4. If you are going to use a remote app, pair it following the procedure
    described below


## Supported clients

forked-daapd supports these kinds of clients:

- DAAP clients, like iTunes or Rhythmbox
- Remote clients, like Apple Remote or compatibles for Android/Windows Phone
- AirPlay devices, like AirPort Express, Shairport and various AirPlay speakers
- RSP clients, like Roku Soundbridge
- MPD clients, like mpc (see [mpd-clients](#mpd-clients))

Like iTunes, you can control forked-daapd with Remote and stream your music
to AirPlay devices.

A single forked-daapd instance can handle several clients concurrently,
regardless of the protocol.

Here is a list of working and non-working DAAP and Remote clients. The list is
probably obsolete when you read it :-)

|          Client          | Developer  |  Type  |   Platform    | Working (vers.) |
| ------------------------ | ---------- | ------ | ------------- | --------------- |
| iTunes                   | Apple      | DAAP   | Win, OSX      | Yes (12.1)      |
| Rhythmbox                | Gnome      | DAAP   | Linux         | Yes             |
| WinAmp DAAPClient        | WardFamily | DAAP   | WinAmp        | Yes             |
| Amarok w/DAAP plugin     | KDE        | DAAP   | Linux/Win     | Yes (2.8.0)     |
| Banshee                  |            | DAAP   | Linux/Win/OSX | No (2.6.2)      |
| jtunes4                  |            | DAAP   | Java          | No              |
| Firefly Client           |            | (DAAP) | Java          | No              |
| Remote                   | Apple      | Remote | iOS           | Yes (4.2.1)     |
| Retune                   | SquallyDoc | Remote | Android       | Yes (3.5.23)    |
| TunesRemote+             | Melloware  | Remote | Android       | Yes (2.5.3)     |
| Remote for iTunes        | Hyperfine  | Remote | Android       | Yes             |
| Remote for Windows Phone | Komodex    | Remote | Windows Phone | Yes (2.2.1.0)   |
| TunesRemote SE           |            | Remote | Java          | Yes (r108)      |



## Using Remote

If you plan to use Remote with forked-daapd, read the following sections
carefully. The pairing process described is similar for other controllers, but
some do not require pairing.

### Pairing with Remote on iPod/iPhone

forked-daapd can be paired with Apple's Remote application for iPod/iPhone/iPad;
this is how the pairing process works:

 1. Start forked-daapd
 2. Start Remote, go to Settings, Add Library
 3. Look in the log file for a message saying:
    
    ```
    "Discovered remote 'Foobar' (id 71624..."
    ```
   
    This tells you the name of your device (Foobar in this example).
    
    If you cannot find this message, it means that forked-daapd did not receive
    a mDNS announcement from your Remote. You have a network issue and mDNS
    doesn't work properly on your network.
    
 4. Prepare a text file with a filename ending with .remote; the filename
    doesn't matter, only the .remote ending does. This file must contain
    two lines: the first line is the name of your iPod/iPhone/iPad, the second
    is the 4-digit pairing code displayed by Remote.
    
    If your iPod/iPhone/iPad is named "Foobar" and Remote gives you the pairing
    code 5387, the file content must be:
    ```
    Foobar
    5387 
    ```
    
 5. Move this file somewhere in your library

At this point, you should be done with the pairing process and Remote should
display the name of your forked-daapd library. You should delete the .remote
file once the pairing process is done.

If Remote doesn't display the name of your forked-daapd library at this point,
the pairing process failed. Here are some common reasons:

#### Your library is a network mount
forked-daapd does not get notified about new files on network mounts, so the
.remote file was not detected. You will see no log file messages about the file.
Solution: Set two library paths in the config, and add the .remote file to the
local path. See [Libraries on network mounts](#libraries-on-network-mounts).

#### You did not enter the correct name or pairing code
You will see an error in the log about pairing failure with a HTTP response code
that is not 0.
Solution: Copy-paste the name to be sure to get specials characters right. You
can also try the pairinghelper script located in the scripts-folder of the
source.

#### No response from Remote, possibly a network issue
If you see an error in the log with a HTTP response code that is 0 it means that
forked-daapd could not establish a connection to Remote. This might be a network
issue.
Solution: You can use avahi-browse for troubleshooting:
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

The name of your iPod/iPhone/iPad is the value of the DvNm field above. In this
example, the correct value is Foobar. To check for network issues you can try to
connect to address and port with telnet.

### Selecting output devices

Remote gets a list of output devices from the server; this list includes any
and all devices on the network we know of that advertise AirPlay: AirPort
Express, Apple TV, ... It also includes the local audio output, that is, the
sound card on the server (even if there is no soundcard).

By default, if no output is selected when playback starts, the local output
device will be used. If that fails it will try to stream to any available
AirPlay speaker.

forked-daapd remembers your selection and the individual volume for each
output device; selected devices will be automatically re-selected at the next
server startup, provided they appear in the 5 minutes following the startup
and no playback has occured yet.


## AirPlay devices/speakers

forked-daapd will discover the AirPlay devices available on your network. For
devices that are password-protected, the device's AirPlay name and password
must be given in the configuration file. See the sample configuration file
for the syntax.


## Local audio output

The audio section of the configuration file supports 2 parameters for the local
audio device:
 - nickname: this is the name that will be used in the speakers list in Remote
 - card: this is the name/device string (ALSA) or device node (OSS4) to be used
   as the local audio device. Defaults to "default" for ALSA and "/dev/dsp" for
   OSS4.


## Supported formats

forked-daapd should support pretty much all media formats. It relies on libav
(ffmpeg) to extract metadata and decode the files on the fly when the client
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


## Streaming MPEG4

Depending on the client application, you may need to optimize your MPEG4 files
for streaming. Stream-optimized MPEG4 files have their metadata at the beginning
of the file, whereas non-optimized files have them at the end.

Not all clients need this; if you're having trouble playing your MPEG4 files,
this is the most probable cause. iTunes, in particular, doesn't handle files
that aren't optimized, though FrontRow does.

Files produced by iTunes are always optimized by default. Files produced by
FAAC and a lot of other encoders are not, though some encoders have an option
for that.

The mp4creator tool from the mpeg4ip suite can be used to optimize MPEG4 files,
with the -optimize option:
```
  $ mp4creator -optimize foo.m4a
```

Don't forget to make a backup copy of your file, just in case.

Note that not all tag/metadata editors know about stream optimization and will
happily write the metadata back at the end of the file after you've modified
them. Watch out for that.


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
documented in [README_SMARTPL.md](README_SMARTPL.md).


## Artwork

forked-daapd has support for artwork.

Embedded artwork is only supported if your version of forked-daapd was built
with libav 9+ or ffmpeg 0.11+.

Your artwork must be in PNG or JPEG format, dimensions do not matter;
forked-daapd scales down (never up) the artwork on-the-fly to match the
constraints given by the client. Note, however, that the bigger the picture,
the more time and resources it takes to perform the scaling operation.

The naming convention for album and artist artwork (group artwork) is as 
follows:
 - if a file {artwork,cover,Folder}.{png,jpg} is found in one of the directories
   containing files that are part of the group, it is used as the artwork. The
   first file found is used, ordering is not guaranteed;
 - failing that, if [directory name].{png,jpg} is found in one of the
   directories containing files that are part of the group, it is used as the
   artwork. The first file found is used, ordering is not guaranteed;
 - failing that, individual files are examined and the first file found 
   with an embedded artwork is used. Here again, ordering is not guaranteed.

Artwork for individual songs is not supported, artwork for individual songs is 
found by resolving to the group artwork.

{artwork,cover,Folder} are the default, you can add other base names in the
configuration file.

You can use symlinks for the artwork files; the artwork is not scanned/indexed.

forked-daapd caches artwork in a separate cache file. The default path is 
`/var/cache/forked-daapd/cache.db` and can be configured in the configuration 
file. The cache.db file can be deleted without losing the library and pairing 
informations.

## Library

The library is scanned in bulk mode at startup, but the server will be available
even while this scan is in progress. You can follow the progress of the scan in
the log file.

Of course, if files have gone missing while the server was not running a request
for these files will produce an error until the scan has completed and the file
is no longer offered. Similarly, new files added while the server was not
running won't be offered until they've been scanned.

Changes to the library are reflected in real time after the initial scan. The
directories are monitored for changes and rescanned on the fly. Note that if you
have your library on a network mount then real time updating may not work. Read
below about what to do in that case.

If you change any of the directory settings in the library section of the
configuration file a rescan is required before the new setting will take effect.
Currently, this will not be done automatically, so you need to trigger the
rescan as described below.

Symlinks are supported and dereferenced. This does interact in tricky ways
with the above monitoring and rescanning, so you've been warned. Changes to
symlinks themselves won't be taken into account, or not the way you'd expect.

If you use symlinks, do not move around the target of the symlink. Avoid
linking files, as files themselves aren't monitored for changes individually,
so changes won't be noticed unless the file happens to be in a directory that
is monitored.

Bottom line: symlinks are for directories only.

Pipes made with mkfifo are also supported. This feature can be useful if you
have a program that can stream PCM16 audio to a pipe. Forked-daapd can then
forward the audio to one or more AirPlay speakers.

Pipes have no metadata, so they will be added with "Unknown artist" and "Unknown
album". The name of the pipe will be used as the track title.


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



## Command line and web interface

forked-daapd is meant to be used with the clients mentioned above, so it does
not have a command line interface nor does it have a web interface. You can,
however, to some extent control forked-daapd with [MPD clients](#mpd-clients) or 
from the command line by issuing DAAP/DACP commands with a program like curl. Here 
is an example of how to do that.

Say you have a playlist with a radio station, and you want to make a script that
starts playback of that station:

1. Run 'sqlite3 [your forked-daapd db]'. Use 'select id,title from files' to get
   the id of the radio station, and use 'select id,title from playlists' to get the
   id of the playlist.
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

forked-daapd has *some* support for Spotify. It must be compiled with the
`--enable-spotify option` (see [INSTALL](INSTALL)). You must have also have libspotify
installed, otherwise the Spotify integration will not be available. You can
get libspotify here:

  - Original (binary) tar.gz, see https://developer.spotify.com
  - Debian package (libspotify-dev), see https://apt.mopidy.com
  
You must also have a Spotify premium account. If you normally log into Spotify
with your Facebook account you must first go to Spotify's web site where you can
get the Spotify username and password that matches your account. With
forked-daapd you cannot login into Spotify with your Facebook username/password.

The procedure for logging in to Spotify is very much like the Remote pairing
procedure. You must prepare a file, which should have the ending ".spotify".
The file must have two lines: The first is your Spotify user name, and the
second is your password. Move the file to your forked-daapd library.
Forked-daapd will then log in and add all the music in your Spotify playlists
to its database.

Spotify will automatically notify forked-daapd about playlist updates, so you
should not need to restart forked-daapd to syncronize with Spotify.

For safety you should delete the ".spotify" file after first login. Forked-daapd
will not store your password, but will still be able to log you in automatically
afterwards, because libspotify saves a login token. You can configure the
location of your Spotify user data in the configuration file.

Limitations: You will only be able to play tracks from your Spotify playlists,
so you can't search and listen to music from the rest of the Spotify catalogue.
You will not be able to do any playlist management through forked-daapd - use
a Spotify client for that. You also can only listen to your music by letting
forked-daapd do the playback - so that means you can't stream from forked-daapd
to iTunes.


## LastFM

If forked-daapd was built with LastFM scrobbling enabled (see the [INSTALL](INSTALL) file)
you can have it scrobble the music you listen to. To set up scrobbling you must
create a text file with the file name ending ".lastfm". The file must have two
lines: The first is your LastFM user name, and the second is your password. Move
the file to your forked-daapd library. Forked-daapd will then log in and get a
permanent session key.

You should delete the .lastfm file immediately after completing the first login.
For safety, forked-daapd will not store your LastFM username/password, only the
session key. The session key does not expire.

To stop scrobbling from forked-daapd, add an empty ".lastfm" file to your
library.

## MPD clients
If forked-daapd was build with support for the [Music Player Deamon](http://musicpd.org/) 
protocol (see the [INSTALL](INSTALL) file) you can - to some extent - use clients for MPD 
to control forked-daapd. 
By default forked-daapd listens on port 6600 for MPD clients. You can change this by
adding a section "mpd" to the forked-daapd.conf file:

```
# MPD configuration (only have effect if MPD enabled - see README/INSTALL)
mpd {
	port = 8800
}
```

Currently only a subset of the commands offered by MPD (see [MPD protocol documentation](http://www.musicpd.org/doc/protocol/)) 
are supported by forked-daapd.

Due to some differences between forked-daapd and MPD not all commands will act the same way they would running MPD:

- consume, crossfade, mixrampdb, mixrampdelay and replaygain will have no effect
- single, repeat: unlike MPD forked-daapd does not support setting single and repeat separately 
  on/off, instead repeat off, repeat all and repeat single are supported. Thus setting single on 
  will result in repeat single, repeat on results in repeat all.

Following table shows what is working for a selection of MPD clients:

|          Client                               |  Type  | Status          |
| --------------------------------------------- | ------ | --------------- |
| [mpc](http://www.musicpd.org/clients/mpc/)    | CLI    | Working commands: mpc, add, crop, current, del (ranges are not yet supported), play, next, prev (behaves like cdprev), pause, toggle, cdprev, seek, clear, outputs, enable, disable, playlist, ls, load, volume, repeat, random, single, search, find, list, update (initiates an init-rescan, the path argument is not supported)   |
| [ympd](http://www.ympd.org/)                  | Web    | Everything except "add stream" should work |




