# Command line

You can choose between:

- a [MPD command line client](#mpd-clients) (easiest) like `mpc`
- curl with OwnTone's JSON API (see [README_JSON_API.md](https://github.com/owntone/owntone-server/blob/master/README_JSON_API.md))
- curl with DAAP/DACP commands (hardest)

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

```
curl "http://localhost:3689/login?pairing-guid=0x1&request-session-id=50"
curl "http://localhost:3689/ctrl-int/1/playspec?database-spec='dmap.persistentid:0x1'&container-spec='dmap.persistentid:0x[PLAYLIST-ID]'&container-item-spec='dmap.containeritemid:0x[FILE ID]'&session-id=50"
curl "http://localhost:3689/logout?session-id=50"
```
