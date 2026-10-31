# Local audio

## Local audio through ALSA

In the config file, you can select ALSA for local audio. This is the default.

When using ALSA, the server will try to synchronize playback with AirPlay. You
can adjust the synchronization in the config file.

For most setups the default values in the config file should work. If they
don't, there is help [here](../advanced/outputs-alsa.md)

## Local audio through PipeWire

In the config file, you can select PipeWire for local audio. PipeWire is
the default audio system on most current Linux distributions; if `wpctl` or
`pactl` already work on your system, it's generally the right choice over
plain ALSA or PulseAudio.

PipeWire also handles Bluetooth output similarly to PulseAudio - once
a Bluetooth speaker is paired and connected (e.g. via `bluetoothctl`), it
appears to PipeWire as a regular sink, and OwnTone can target it like any
other. If it is the default sink, OwnTone will use it with minimal configuration

By default OwnTone's volume slider controls its own stream's software gain.
To have it drive the shared system sink volume instead (the same one
`wpctl set-volume` controls), some extra config is needed - see
[PipeWire](../advanced/outputs-pipewire.md).
 
## Local audio, Bluetooth and more through PulseAudio

In the config file, you can select PulseAudio for local audio. In addition to
local audio, PulseAudio also supports an array of other targets, e.g. Bluetooth
or DLNA. However, PulseAudio does require some setup, so here is a separate page
with some help on that: [PulseAudio](../advanced/outputs-pulse.md)

Note that if you select PulseAudio the "card" setting in the config file has
no effect. Instead all sound cards detected by PulseAudio will be listed as
speakers by OwnTone.

You can adjust the latency of PulseAudio playback in the config file.
