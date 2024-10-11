# Desktop Remote Control

To control OwnTone from Linux, Windows or Mac, you can use:

- [The web interface](#the-web-interface)
- [A remote for iTunes/Apple Music](#remotes-for-itunesapple-music)
- [A MPD client](#mpd-clients)

The web interface is the most feature complete and works on all platforms, so
on desktop there isn't much reason to use anything else.

However, instead of a remote control application, you can also connect to
OwnTone via a Media Client e.g. iTunes or Apple Music. Media clients will get
the media from OwnTone and do the playback themselves (remotes just control
OwnTone playback). See [Media Clients](../media-clients.md) for more
information.


## The web interface

See [web interface](web.md).


## Remotes for iTunes/Apple Music

There are only a few of these, see the below table.

|          Client          | Developer   |  Type  |   Platform      | Working (vers.) |
| ------------------------ | ----------- | ------ | --------------- | --------------- |
| TunesRemote SE           |             | Remote | Java            | Yes (r108)      |
| rtRemote for Windows     | bizmodeller | Remote | Windows         | Yes (1.2.0.67)  |


## MPD clients

There's a range of MPD clients available that also work with OwnTone e.g.
Cantata and Plattenalbum.
 
The better ones support local playback, speaker control, artwork and automatic
discovery of OwnTone's MPD server.

By default OwnTone listens on port 6600 for MPD clients. You can change
this in the configuration file.

Due to some differences between OwnTone and MPD not all commands will act the
same way they would running MPD:

- crossfade, mixrampdb, mixrampdelay and replaygain will have no effect
- single, repeat: unlike MPD, OwnTone does not support setting single and repeat
  separately on/off, instead repeat off, repeat all and repeat single are
  supported. Thus setting single on will result in repeat single, repeat on
  results in repeat all.
