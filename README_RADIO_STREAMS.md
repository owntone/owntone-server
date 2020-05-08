# forked-daapd and Radio Stream configuration.

Radio streams have many different ways in how metadata is sent.  Many should just work as expected, but a few may require some tweaking. If you are not seeing expected title, track, artist, artwork in forked-daapd clients or webui, the following may help.

First, understand what and how the particular stream is sending information.
ffprobe is a command that can be used to interegrate most of the stream information.
`ffprobe <http://stream.url>` should give you some useful output, look at the Metadata section, below is an example.

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

In the example above, all tags are populated with correct information, no modifications to forked-daapd configuration should be needed.
Note that StreamUrl points to the artwork image file.


Below is another example that will require some tweaks to forked-daapd, Notice `icy-name` is blank and `StreamUrl` doesn't point to an image
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
### 1) Stream Name/Title
forked-daapd will use the URL as the name of the stream if it's not embedded in the ice-name tag, if
you want to change that, then you can use the m3u tags in the playlist file that points to stream url to fix this.<br>
Example `My Radio Stream.m3u`:-
```
#EXTM3U
#EXTINF:-1, - My Radio Stream Name
http://radio.stream.domain/stream.url
```
Please search details of the #EXTINF tag, but the format is basically `#EXTINF:<length>, <Artist Name> - <Artist Title>`. Length is -1 since it's a stream, `<Artist Name>` was left blank since `StreamTitle` is accurate in the Metadata but `<Artist Title>` was populated to `My Radio Stream Name` since `icy-name` was blank. This way forkard-daapd will show `My Radio Stream Name` rather than the URL for the stream name.

Next, modify forked-daapd configuration and make sure `m3u_overrides = true` is set, then forked-daapd will use the .mru

### 2) Artwork.
The `StreamUrl` metatag is used for artwork, but in the above example that doesn't point to a valid image file.
First simply get the artwork for the stream and name it .png or .jpg with the same filename as the .m3u plaulist.  Example `My Radio Stream.m3u` and `My Radio Stream.jpg`.  forked-daapd will now display that artwork.
Some streams as in the example above will use the `StreamUrl` tag to point to another page with extended information about the current track. If you use curl / wget / web browser to hit the url you will see an output similar to
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
From that output you can tell that `eventImageUrl` holds the artwork. modify `forkard-daapd.conf` and look at `stream_urlimage_tags = { "eventImageUrl", "StreamURL", "ImageUrl" }`, these are a list of tags forked-daapd will use to find valid artwork.  In this example the tag `eventImageUrl` will match and the artwork `"https://radio.stream.domain/artist/1-1/320x320/562.jpg?ver=1465083491"` will be used.
Simply modify `stream_urlimage_tags` and add any image tags for the particular stream(s) you are artwork.



