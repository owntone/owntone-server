# forked-daapd and Compilation Albums

Compilation albums that have multiple artists can be handled in a number of ways
such as a user specified metadata where `album artist` == `Various Artists`.
However this approach does not allow `forked daapd` to group the tracks together
in the web UI album view.  

Automated metadata taggers such as MusicBrainz and CDDB can identify 
compliations and add an unique identifier which `forked daapd` can use over the
album name when grouping.


## Adding 'Compilation Tags'

Users can use any [MusicBrainz](https://musicbrainz.org) [enabled metadata tagger](https://musicbrainz.org/doc/MusicBrainz_Enabled_Applications#Taggers) (such as Picard) to add 
the relevant tags to the compilation album's files OR users can manually add 
these compilation identifing tags using something like [ffmpeg](https://www.fffmpeg.org); the only requirement is that
within the music library these compilation tags are unique.

`forked daapd` can use any of the the following metadata tag names (in ordre of
preference) to store the compilation tag:

- `MusicBrainz Album Id`
- `MusicBrainz DiscID`
- `CDDB DiscID`
- `MusicBrainz Release Group Id`
- `CATALOGNUMBER`
- `BARCODE"`

`ffmpeg` can be used to manually add the tags if you do not wish to autotag

```
 ffmpeg -hide_banner -i input-basicmeta.mp3 -c copy -metadata "MusicBrainz Album Id"="va-someunique-value" output.mp3
```

Verfication can done via `ffprobe` for a manually updated file:
```
$ ffprobe -hide_banner -i output.mp3
Input #0, mp3, from 'output.mp3':
  Metadata:
    artist          : Marvin Gaye
    title           : Let's Get It On
    album           : The Motown Collection
    genre           : Pop
    MusicBrainz Album Id: va-someunique-value
    encoder         : Lavf58.12.100
  Duration: 00:04:53.98, start: 0.025057, bitrate: 251 kb/s
    Stream #0:0: Audio: mp3, 44100 Hz, stereo, fltp, 251 kb/s
    Metadata:
      encoder         : LAME3.98r
    Side data:
      replaygain: track gain - -2.400000, track peak - unknown, album gain - unknown, album peak - unknown, 
```
and similary for the same (CD ripped) file that was autotagged via [MusicBrainz Picard](https://picard.musicbrainz.org)
```
$ ffprobe -hide_banner -i marvin-gaye-letsgetiton.mp3
Input #0, mp3, from 'marvin-gaye-letsgetiton.mp3':
  Metadata:
    title           : Let's Get It On
    artist          : Marvin Gaye
    track           : 2/15
    album           : The Motown Collection (10 CD/1 DVD) Box set, Enhanced
    disc            : 7/11
    genre           : Pop
    compilation     : 1
    TMED            : CD
    TDOR            : 2008
    date            : 2008
    artist-sort     : Gaye, Marvin
    TSRC            : GBCBR0300006
    SCRIPT          : Latn
    album_artist    : Various Artists
    TSO2            : Various Artists
    TSST            : Volume 4, Disc 1
    ASIN            : B001BK4IUQ
    originalyear    : 2008
    publisher       : Time-Life Music/UMe
    Artists         : Marvin Gaye
    MusicBrainz Album Status: official
    MusicBrainz Album Type: album/compilation
    MusicBrainz Album Id: 389cb65e-87bc-4897-9c56-26a482c4e1d3
    MusicBrainz Artist Id: afdb7919-059d-43c1-b668-ba1d265e7e42
    MusicBrainz Album Artist Id: 89ad4ac3-39f7-470e-963a-56509c546377
    MusicBrainz Release Group Id: 6b2d3944-4bfa-46f2-ae44-18ffce2b32b2
    MusicBrainz Release Track Id: 935f9071-0b57-42a7-b3a2-50fa49e84039
  Duration: 00:04:53.98, start: 0.025057, bitrate: 252 kb/s
    Stream #0:0: Audio: mp3, 44100 Hz, stereo, fltp, 251 kb/s
    Metadata:
      encoder         : LAME3.98r
    Side data:
      replaygain: track gain - -2.400000, track peak - unknown, album gain - unknown, album peak - unknown,
```

