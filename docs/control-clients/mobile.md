# Mobile Remote Control

To control OwnTone from your mobile device, you can use:

- [The web interface](#the-web-interface)
- [(iOS) The Remote app from Apple](#apple-remote-app-ios)
- [(Android) An iTunes/Apple Music remote app](#remotes-for-itunesapple-music-android)
- [A MPD client app](#mpd-client-apps)

The web interface is the most feature complete, but apps may have UX advantages.
The table below shows how some features compare.

| Feature                               | Remote     | MPD client | Web        |
| ------------------------------------- | ---------- | ---------- | ---------- |
| Browse library                        | yes        | yes        | yes        |
| Control playback and queue            | yes        | yes        | yes        |
| Artwork                               | yes        | yes        | yes        |
| Individual speaker selection          | yes        | some       | yes        |
| Individual speaker volume             | yes        | no         | yes        |
| Volume control using phone buttons    | no         | ?          | no         |
| Listen on phone                       | no         | some       | yes        |
| Access non-library Spotify tracks     | no         | no         | yes        |
| Edit and save m3u playlists           | no         | some       | yes        |

While OwnTone supports playing tracks from Spotify, there is no support for
Spotify Connect, so you can't control from the Spotify app.


## The web interface

See [web interface](web.md).


## Apple Remote app (iOS)

Remote gets a list of output devices from the server; this list includes any
and all devices on the network we know of that advertise AirPlay: AirPort
Express, Apple TV, … It also includes the local audio output, that is, the
sound card on the server (even if there is no sound card).

OwnTone remembers your selection and the individual volume for each
output device; selected devices will be automatically re-selected, except if
they return online during playback.

### Pairing

1. Open the [web interface](web.md) at either [http://owntone.local:3689](http://owntone.local:3689)
   or `http://SERVER-IP-ADDRESS:3689`
2. Start Remote, go to Settings, Add Library
3. Enter the pair code in the web interface (reload the browser page if
   it does not automatically pick up the pairing request)

If Remote does not connect to OwnTone after you entered the pairing code
something went wrong. Check the log file to see the error message. Here are
some common reasons:

- You did not enter the correct pairing code

  You will see an error in the log about pairing failure with a HTTP response code
  that is *not* 0.

  Solution: Try again.

- No response from Remote, possibly a network issue

  If you see an error in the log with either:

  - a HTTP response code that is 0
  - "Empty pairing request callback"

  it means that OwnTone could not establish a connection to Remote. This
  might be a network issue, your router may not be allowing multicast between the
  Remote device and the host OwnTone is running on.

  Solution 1: Sometimes it resolves the issue if you force Remote to quit, restart
  it and do the pairing process again. Another trick is to establish some other
  connection (eg SSH) from the iPod/iPhone/iPad to the host.

  Solution 2: Check your router settings if you can whitelist multicast addresses
  under IGMP settings. For Apple Bonjour, setting a multicast address of
  224.0.0.251 and a netmask of 255.255.255.255 should work.

- Otherwise try using `avahi-browse` for troubleshooting:
  
  - in a terminal, run:

    ```shell
    avahi-browse -r -k _touch-remote._tcp
    ```

  - start Remote, goto Settings, Add Library
  - after a couple seconds at most, you should get something similar to this:

    ```shell
    + ath0 IPv4 59eff13ea2f98dbbef6c162f9df71b784a3ef9a3      _touch-remote._tcp   local
    = ath0 IPv4 59eff13ea2f98dbbef6c162f9df71b784a3ef9a3      _touch-remote._tcp   local
       hostname = [Foobar.local]
       address = [192.168.1.1]
       port = [49160]
       txt = ["DvTy=iPod touch" "RemN=Remote" "txtvers=1" "RemV=10000" "Pair=FAEA410630AEC05E" "DvNm=Foobar"]
    ```

    Hit Ctrl+C to terminate `avahi-browse`.

- To check for network issues you can try to connect to the server address and
  port with [`nc`](https://en.wikipedia.org/wiki/Netcat) or
  [`telnet`](https://en.wikipedia.org/wiki/Telnet) commands.


## Remotes for iTunes/Apple Music (Android)

The below Android remote apps work with OwnTone.

|          Client          | Developer   |  Type  | Working (vers.) |
| ------------------------ | ----------- | ------ | --------------- |
| Retune                   | SquallyDoc  | Remote | Yes (3.5.23)    |
| TunesRemote+             | Melloware   | Remote | Yes (2.5.3)     |
| Remote for iTunes        | Hyperfine   | Remote | Yes             |

For usage and troubleshooting details, see the instructions for [Apple Remote](#apple-remote-app-ios).


## MPD client apps

There's a range of MPD clients available from app store that also work with
OwnTone e.g. MPD Pilot, MaximumMPD, Rigelian and Stylophone.

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
