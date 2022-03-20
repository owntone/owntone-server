# Supported clients

OwnTone supports these kinds of clients:

- DAAP clients, like iTunes or Rhythmbox
- Remote clients, like Apple Remote or compatibles for Android/Windows Phone
- AirPlay devices, like AirPort Express, Shairport and various AirPlay speakers
- Chromecast devices
- MPD clients, like mpc (see [mpd-clients](#mpd-clients))
- MP3 network stream clients, like VLC and almost any other music player
- RSP clients, like Roku Soundbridge

Like iTunes, you can control OwnTone with Remote and stream your music to
AirPlay devices.

A single OwnTone instance can handle several clients concurrently, regardless of
the protocol.

By default all clients on 192.168.* (and the ipv6 equivalent) are allowed to
connect without authentication. You can change that in the configuration file.

Here is a list of working and non-working DAAP and Remote clients. The list is
probably obsolete when you read it :-)

|          Client          | Developer   |  Type  |   Platform    | Working (vers.) |
| ------------------------ | ----------- | ------ | ------------- | --------------- |
| iTunes                   | Apple       | DAAP   | Win           | Yes (12.10.1)   |
| Apple Music              | Apple       | DAAP   | MacOS         | Yes              |
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

