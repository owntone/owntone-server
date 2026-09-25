# PipeWire

OwnTone can output local audio through [PipeWire](https://pipewire.org/),
the audio server used by default on most current Linux distributions. If
`wpctl` or `pactl` already work on your system, PipeWire is generally the
right choice.

Select it with `type = "pipewire"` in the `audio` section of the config
file. By default OwnTone's volume slider does nothing; to have it actually
control PipeWire's volume, see [PipeWire](../advanced/outputs-pipewire.md)
for the available options.

If your system is still on plain PulseAudio (not `pipewire-pulse`), use
[PulseAudio](../advanced/outputs-pulse.md) instead.
