# Media Clients

Media Clients are applications that download the media from the server and do
the playback themselves. OwnTone supports media clients via the DAAP and RSP
protocols (so not UPNP).

Some Media Clients are also able to play video from OwnTone.

OwnTone can't serve Spotify, internet radio and streams to Media Clients. For
that you must let OwnTone do the playback.

Here is a list of working and non-working DAAP clients. The list is probably
obsolete when you read it :-)

|          Client          | Developer   |  Type  |   Platform      | Working (vers.) |
| ------------------------ | ----------- | ------ | --------------- | --------------- |
| iTunes                   | Apple       | DAAP   | Win             | Yes (12.10.1)   |
| Apple Music              | Apple       | DAAP   | macOS           | Yes              |
| Rhythmbox                | Gnome       | DAAP   | Linux           | Yes             |
| Diapente                 | diapente    | DAAP   | Android         | Yes             |
| WinAmp DAAPClient        | WardFamily  | DAAP   | WinAmp          | Yes             |
| Amarok w/DAAP plugin     | KDE         | DAAP   | Linux/Win       | Yes (2.8.0)     |
| Banshee                  |             | DAAP   | Linux/Win/macOS | No (2.6.2)      |
| jtunes4                  |             | DAAP   | Java            | No              |
| Firefly Client           |             | (DAAP) | Java            | No              |

Technically, devices like the Roku Soundbridge are both media clients and
audio outputs. You can find information about them [here](audio-outputs/roku.md).
