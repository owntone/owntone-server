# PipeWire

PipeWire is the audio/video server used by default on most current Linux
distributions. If `pactl` or `wpctl` work on your system, you're already
running it, and it's generally the right choice for local audio - OwnTone
connects as a native PipeWire client and lets WirePlumber decide which sink
the audio goes to, the same way any other desktop application's audio does.

If your system is still on plain PulseAudio (not `pipewire-pulse`), use
[PulseAudio](outputs-pulse.md) instead. To address an ALSA device directly
without going through PipeWire, use [ALSA](outputs-alsa.md).

### Step 1: Select PipeWire

```conf
audio {
    nickname = "Computer"
    type = "pipewire"
}
```

With no further configuration, OwnTone opens a PipeWire stream and lets
WirePlumber route it to the current default sink. OwnTone's volume slider
controls only its own stream's software gain (`stream` mode, the default) -
independent of the system volume and any other application.

### Step 2: Choose a volume control mode (optional)

To have OwnTone's volume slider actually control something, set `mixer`:

```conf
audio {
    nickname = "Computer"
    type = "pipewire"
    mixer = "sink"
}
```

| Value | Effect |
|---|---|
| *(unset)*, `stream` | OwnTone controls only its own stream's software gain. Independent of other applications, and works even on sinks with no hardware mixer control. This is the default. |
| `sink` | OwnTone drives the actual sink's volume - the same one `wpctl set-volume` and desktop mixers control. Shared system-wide, and persists across restarts since WirePlumber saves it. |

An unrecognized value is logged as a warning at startup and treated as
`stream`.

### Step 3: Pin a specific sink (optional, `sink` mode only)

By default OwnTone follows PipeWire's current default sink. To pin volume
control to a specific sink instead, set `sink_target` to that sink's
`node.name`:

```conf
    sink_target = "alsa_output.platform-soc_sound.stereo-fallback"
```

List available sink names with:

```shell
wpctl status
```
This also works for Bluetooth sinks - once paired and connected, a
Bluetooth speaker's `node.name` looks like
`bluez_output.XX_XX_XX_XX_XX_XX.1` and can be pinned the same way.

### Step 4: Adjust the volume curve (optional, `sink` mode only)

WirePlumber applies volume on a cubic scale, so `wpctl set-volume 0.5`
sounds roughly "half as loud" rather than "half the amplitude". OwnTone
matches this by default (`cubic`), so its volume percentage lines up with
`wpctl get-volume` for the same sink. Set `sink_volume_curve = "linear"` if
you'd rather write the percentage straight into the sink's gain with no
curve applied.

### Full example

```conf
audio {
    nickname = "Computer"
    type = "pipewire"

    mixer = "sink"                   # "stream" (default) or "sink"
    sink_target = ""                 # node.name to pin to; unset follows the default sink
    sink_volume_curve = "cubic"      # "cubic" (default) or "linear"
}
```
