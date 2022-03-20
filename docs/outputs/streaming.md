# MP3 network streaming (streaming to iOS)

You can listen to audio being played by OwnTone by opening this network
stream address in pretty much any music player:

 http://[your hostname/ip address]:3689/stream.mp3

This is currently the only way of listening to your audio on iOS devices, since
Apple does not allow AirPlay receiver apps, and because Apple Home Sharing
cannot be supported by OwnTone. So what you can do instead is install a
music player app like VLC, connect to the stream and control playback with
Remote.

In the speaker selection list, clicking on the icon should start the stream
playing in the background on browsers that support that.

Note that MP3 encoding must be supported by ffmpeg/libav for this to work. If
it is not available you will see a message in the log file. In Debian/Ubuntu you
get MP3 encoding support by installing the package "libavcodec-extra".
