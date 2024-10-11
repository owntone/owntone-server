# Listening to Audio in a Browser

To listen to audio being played by OwnTone in a browser, follow these
steps:

1. Start playing audio in OwnTone.
2. In the web interface, activate the stream in the output menu by clicking
   on the icon :material-broadcast: next to HTTP Stream.
   After a few seconds, the audio should play in the background [^1].

![Outputs](../assets/images/screenshot-outputs.png){: class="zoom" }

For the streaming option to work, MP3 encoding must be supported by
`libavcodec`. If it is not, a message will appear in the log file. For example,
on Debian or Ubuntu, MP3 encoding support is provided by the package
`libavcodec-extra`.

[^1]: On iOS devices, playing audio in the background when the device is locked
      is not supported in a private browser tab.
