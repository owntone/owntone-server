# API and Command Line

You can choose between:

- [The JSON API](#json-api)
- [A MPD command line client like mpc](#mpc)
- [DAAP/DACP commands](#daapdacp)

The JSON API is the most versatile and the recommended method, but for simple
command line operations, mpc is easier. DAAP/DACP is only for masochists.


## JSON API

See the [JSON API docs](../json-api.md)


## mpc

[mpc](https://www.musicpd.org/clients/mpc/) is easy to use for simple operations
like enabling speakers, changing volume and getting status.

Due to differences in implementation between OwnTone and MPD, some mpc commands
will work differently or not at all.


## DAAP/DACP

Here is an example of how to use curl with DAAP/DACP. Say you have a playlist
with a radio station, and you want to make a script that starts playback of that
station:

1. Run `sqlite3 [your OwnTone db]`. Use `select id,title from files` to get
    the id of the radio station, and use `select id,title from playlists` to get
    the id of the playlist.
2. Convert the two ids to hex.
3. Put the following lines in the script with the relevant ids inserted (also
    observe that you must use a session-id < 100, and that you must login and
    logout):

```shell
curl "http://localhost:3689/login?pairing-guid=0x1&request-session-id=50"
curl "http://localhost:3689/ctrl-int/1/playspec?database-spec='dmap.persistentid:0x1'&container-spec='dmap.persistentid:0x[PLAYLIST-ID]'&container-item-spec='dmap.containeritemid:0x[FILE ID]'&session-id=50"
curl "http://localhost:3689/logout?session-id=50"
```
