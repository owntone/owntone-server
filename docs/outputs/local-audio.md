# Local audio

## Local audio through ALSA

In the config file, you can select ALSA for local audio. This is the default.

When using ALSA, the server will try to synchronize playback with AirPlay. You
can adjust the synchronization in the config file.

For most setups the default values in the config file should work. If they
don't, there is help [here](../advanced/outputs-alsa.md)

## Local audio, Bluetooth and more through PulseAudio

In the config file, you can select PulseAudio for local audio. In addition to
local audio, PulseAudio also supports an array of other targets, e.g. Bluetooth
or DLNA. However, PulseAudio does require some setup, so here is a separate page
with some help on that: [PulseAudio](../advanced/outputs-pulse.md)

Note that if you select PulseAudio the "card" setting in the config file has
no effect. Instead all sound cards detected by PulseAudio will be listed as
speakers by OwnTone.

You can adjust the latency of PulseAudio playback in the config file.
