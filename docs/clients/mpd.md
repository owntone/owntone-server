# MPD clients

You can - to some extent - use clients for MPD to control OwnTone.

By default OwnTone listens on port 6600 for MPD clients. You can change
this in the configuration file.

Currently only a subset of the commands offered by MPD (see [MPD protocol documentation](http://www.musicpd.org/doc/protocol/)) 
are supported.

Due to some differences between OwnTone and MPD not all commands will act the
same way they would running MPD:

- crossfade, mixrampdb, mixrampdelay and replaygain will have no effect
- single, repeat: unlike MPD, OwnTone does not support setting single and repeat separately 
  on/off, instead repeat off, repeat all and repeat single are supported. Thus setting single on 
  will result in repeat single, repeat on results in repeat all.

The following table shows what is working for a selection of MPD clients:

|          Client                               |  Type  | Status          |
| --------------------------------------------- | ------ | --------------- |
| [mpc](http://www.musicpd.org/clients/mpc/)    | CLI    | Working commands: mpc, add, crop, current, del (ranges are not yet supported), play, next, prev (behaves like cdprev), pause, toggle, cdprev, seek, clear, outputs, enable, disable, playlist, ls, load, volume, repeat, random, single, search, find, list, update (initiates an init-rescan, the path argument is not supported)   |
| [ympd](http://www.ympd.org/)                  | Web    | Everything except "add stream" should work |
