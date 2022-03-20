# Local audio

## Local audio through ALSA

In the config file, you can select ALSA for local audio. This is the default.

When using ALSA, the server will try to syncronize playback with AirPlay. You
can adjust the syncronization in the config file.

For most setups the default values in the config file should work. If they
don't, there is help [here](../advanced/outputs-alsa.md)


## Local audio, Bluetooth and more through Pulseaudio

In the config file, you can select Pulseaudio for local audio. In addition to
local audio, Pulseaudio also supports an array of other targets, e.g. Bluetooth
or DLNA. However, Pulseaudio does require some setup, so here is a separate page
with some help on that: [Pulse audio](../advanced/outputs-pulse.md)

Note that if you select Pulseaudio the "card" setting in the config file has
no effect. Instead all soundcards detected by Pulseaudio will be listed as
speakers by OwnTone.

You can adjust the latency of Pulseaudio playback in the config file.
