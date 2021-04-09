# OwnTone and Radio Stream tweaking

Radio streams have many different ways in how metadata is sent.  Many should
just work as expected, but a few may require some tweaking. If you are not
seeing expected title, track, artist, artwork in clients or web UI, the
following may help.

First, understand what and how the particular stream is sending information.
ffprobe is a command that can be used to interegrate most of the stream
information. `ffprobe <http://stream.url>` should give you some useful output,
look at the Metadata section, below is an example.

```
 Metadata:
    icy-br          : 320
    icy-description : DJ-mixed blend of modern and classic rock, electronica, world music, and more. Always 100% commercial-free
    icy-genre       : Eclectic
    icy-name        : Radio Paradise (320k aac)
    icy-pub         : 1
    icy-url         : https://radioparadise.com
    StreamTitle     : Depeche Mode - Strangelove
    StreamUrl       : http://img.radioparadise.com/covers/l/B000002LCI.jpg
```

In the example above, all tags are populated with correct information, no
modifications to the server configuration should be needed. Note that
StreamUrl points to the artwork image file.


Below is another example that will require some tweaks to the server, Notice
`icy-name` is blank and `StreamUrl` doesn't point to an image.

```
Metadata:
    icy-br          : 127
    icy-pub         : 0
    icy-description : Unspecified description
    icy-url         : 
    icy-genre       : various
    icy-name        : 
    StreamTitle     : Pour Some Sugar On Me - Def Leppard
    StreamUrl       : https://radio.stream.domain/api9/eventdata/49790578
```

In the above, first fix is the blank name, second is the image artwork.

### 1) Set stream name/title via the M3U file
Set the name with an EXTINF tag in the m3u playlist file:

```
#EXTM3U
#EXTINF:-1, - My Radio Stream Name
http://radio.stream.domain/stream.url
```

The format is basically `#EXTINF:<length>, <Artist Name> - <Artist Title>`.
Length is -1 since it's a stream, `<Artist Name>` was left blank since
`StreamTitle` is accurate in the Metadata but `<Artist Title>` was set to
`My Radio Stream Name` since `icy-name` was blank.

### 2) StreamUrl is a JSON file with metadata
If `StreamUrl` does not point directly to an artwork file then the link may be
to a json file that contains an artwork link. If so, you can make the server
download the file automatically and search for an artwork link, and also track
duration.

Try to download the file, e.g. with `curl "https://radio.stream.domain/api9/eventdata/49790578"`.
Let's assume you get something like this:

```
{
    "eventId": 49793707,
    "eventStart": "2020-05-08 16:23:03",
    "eventFinish": "2020-05-08 16:27:21",
    "eventDuration": 254,
    "eventType": "Song",
    "eventSongTitle": "Pour Some Sugar On Me",
    "eventSongArtist": "Def Leppard",
    "eventImageUrl": "https://radio.stream.domain/artist/1-1/320x320/562.jpg?ver=1465083491",
    "eventImageUrlSmall": "https://radio.stream.domain/artist/1-1/160x160/562.jpg?ver=1465083491",
    "eventAppleMusicUrl": "https://geo.itunes.apple.com/dk/album/530707298?i=530707313"
}
```

In this case, you would need to tell the server to look for "eventDuration"
and "eventImageUrl" (or just "duration" and "url"). You can do that like this:

```
curl -X PUT "http://localhost:3689/api/settings/misc/streamurl_keywords_length" --data "{\"name\":\"streamurl_keywords_length\",\"value\":\"duration\"}"
curl -X PUT "http://localhost:3689/api/settings/misc/streamurl_keywords_artwork_url" --data "{\"name\":\"streamurl_keywords_artwork_url\",\"value\":\"url\"}
```

If you want multiple search phrases then comma separate, e.g. "duration,length".

### 3) Set metadata with a custom script
If your radio station publishes metadata via another method than the above, e.g.
just on their web site, then you will have to write a script that pulls the
metadata and then pushes it to the server. To update metadata for the
currently playing radio station use something like this JSON API request:

```shell
curl -X PUT "http://localhost:3689/api/queue/items/now_playing?title=Awesome%20title&artwork_url=http%3A%2F%2Fgyfgafguf.dk%2Fimages%2Fpige3.jpg"
```

If your radio station is not returning any artwork links, you can also just make
a static artwork by placing a png/jpg in the same directory as the m3u, and with
the same name, e.g. `My Radio Stream.jpg` for `My Radio Stream.m3u`.
