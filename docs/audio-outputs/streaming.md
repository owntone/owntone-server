# Streaming

The streaming option is useful when you want to listen to audio played by
OwnTone in a browser or a media player of your choice [^1],[^2].

You can control playback via the web interface or any of the supported control
clients.

## Listening to Audio in a Media Player

To listen to audio being played by OwnTone in a media player, follow these
steps:

1. Start playing audio in OwnTone.
2. In the web interface, activate the stream in the output menu by clicking
   on the icon :material-broadcast: next to HTTP Stream.
   After a few seconds, the audio should play in the background.
3. Copy the URL behind the :material-open-in-new: icon next to HTTP Stream.
4. Open the copied URL with the media player, e.g., VLC.
   The URL is usually
   [http://owntone.local:3689/stream.mp3](http://owntone.local:3689/stream.mp3)
   or http://SERVER_ADDRESS:3689/stream.mp3

## Notes

[^1]: On iOS devices, the streaming option is the only way of listening to your
      audio, since Apple does not allow AirPlay receiver apps, and because
      Home Sharing cannot be supported by OwnTone.

[^2]: For the streaming option to work, MP3 encoding must be supported by
      `libavcodec`. If it is not, a message will appear in the log file.
      For example, on Debian or Ubuntu, MP3 encoding support is provided by the
      package `libavcodec-extra`.
