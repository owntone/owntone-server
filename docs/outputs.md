# Outputs

## AirPlay devices/speakers

OwnTone will discover the AirPlay devices available on your network. For
devices that are password-protected, the device's AirPlay name and password
must be given in the configuration file. See the sample configuration file
for the syntax.

If your Apple TV requires device verification (always required by Apple TV4 with
tvOS 10.2) then you can do that through Settings > Remotes & Outputs in the web
interface: Select the device and then enter the PIN that the Apple TV displays.

If your speaker is silent when you start playback, and there is no obvious error
message in the log, you can try disabling ipv6 in the config. Some speakers
announce that they support ipv6, but in fact don't (at least not with forked-
daapd).

If the speaker becomes unselected when you start playback, and you in the log
see "ANNOUNCE request failed in session startup: 400 Bad Request", then try
the Apple Home app > Allow Speakers & TV Access > Anyone On the Same Network
(or Everyone).

## Chromecast

OwnTone will discover Chromecast devices available on your network, and you
can then select the device as a speaker. There is no configuration required.

## Local audio

### ALSA

In the config file, you can select ALSA for local audio. This is the default.

When using ALSA, the server will try to syncronize playback with AirPlay. You
can adjust the syncronization in the config file.

For most setups the default values in the config file should work. If they
don't, there is help here: [ALSA](alsa.md)

### Pulseaudio

In the config file, you can select Pulseaudio for local audio. In addition to
local audio, Pulseaudio also supports an array of other targets, e.g. Bluetooth
or DLNA. However, Pulseaudio does require some setup, so here is a separate page
with some help on that: [Pulse Audio](pulse.md)

Note that if you select Pulseaudio the "card" setting in the config file has
no effect. Instead all soundcards detected by Pulseaudio will be listed as
speakers by OwnTone.

You can adjust the latency of Pulseaudio playback in the config file.

## MP3 network streaming (streaming to iOS)

You can listen to audio being played by OwnTone by opening this network
stream address in pretty much any music player:

`http://[your hostname/ip address]:3689/stream.mp3`

This is currently the only way of listening to your audio on iOS devices, since
Apple does not allow AirPlay receiver apps, and because Apple Home Sharing
cannot be supported by OwnTone. So what you can do instead is install a
music player app like VLC, connect to the stream and control playback with
Remote.

Note that MP3 encoding must be supported by ffmpeg/libav for this to work. If
it is not available you will see a message in the log file. In Debian/Ubuntu you
get MP3 encoding support by installing the package "libavcodec-extra".
