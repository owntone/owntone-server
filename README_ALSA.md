# forked-daapd and ALSA

ALSA is one of the main output configuration options for local audio; when using ALSA you will typically let the system select the soundcard on your machine as the `default` device/sound card - a mixer associated with the ALSA device is used for volume control.  However if your machine has multiple sound cards and your system chooses the wrong playback device, you will need to manually select the card and mixer to complete the `forked daapd` configuration.

```
# example audio section for server
audio {
    nickname = "Computer"
    type = "alsa"

    card = "hw:1"           # defaults to 'default'
    mixer = "Analogue"      # defaults to 'PCM' or 'Master'
    mixer_device = "hw:1"   # defaults to same as 'card' value
}
```

To verify if the `default` sound device is correct for playback, we will use the `aplay` utility.

```
# generate some audio if you don't have a wav file to hand
$ sox -n -c 2 -r 44100 -b 16 -C 128 /tmp/sine441.wav synth 30 sin 500-100 fade h 0.2 30 0.2

$ aplay -Ddefault /tmp/sine441.wav
```
If you can hear music played then you are good to use `default` for the server configuration.  If you can not hear anything from the `aplay` firstly verify (using `alsamixer`) that the sound card is not muted.  If the card is not muted AND there is no sound you can try the options below to determine the card and mixer for configuring the server.

The example below is how I determined the correct sound card and mixer values for a Raspberry Pi that has an additional DAC card (hat) mounted.


## Determining the sound cards you have / ALSA can see

Use `aplay -l` to list all the sound cards and their order as known to the system - you can have multiple `card X, device Y` entries; some cards can also have multiple playback devices such as the RPI's onboard soundcard which feeds both headphone (card 0, device 0) and HDMI (card 0, device 1).
```
$ aplay -l
**** List of PLAYBACK Hardware Devices ****
card 0: ALSA [bcm2835 ALSA], device 0: bcm2835 ALSA [bcm2835 ALSA]
  Subdevices: 6/7
  Subdevice #0: subdevice #0
  Subdevice #1: subdevice #1
  Subdevice #2: subdevice #2
  Subdevice #3: subdevice #3
  Subdevice #4: subdevice #4
  Subdevice #5: subdevice #5
  Subdevice #6: subdevice #6
card 0: ALSA [bcm2835 ALSA], device 1: bcm2835 ALSA [bcm2835 IEC958/HDMI]
  Subdevices: 1/1
  Subdevice #0: subdevice #0
card 1: IQaudIODAC [IQaudIODAC], device 0: IQaudIO DAC HiFi pcm512x-hifi-0 []
  Subdevices: 1/1
  Subdevice #0: subdevice #0
```
On this machine we see the second sound card installed, an IQaudIODAC dac hat, and identified as `card 1 device 0`.  This is the playback device we want to be used by the server.

In ALSA configuration terms `card X, device Y` is known as `hw:X,Y` which is the format used for the server configuration: `hw:1,0` is the IQaudIODAC that we want to use - we verify audiable playback through that sound card using `aplay -Dhw:1 /tmp/sine441.wav`.  If the card has only one device, we can simply refer to the sound card using `hw:X` so in this case where the IQaudIODAC only has one device, we can refer to this card as `hw:1` or `hw:1,0`.

Use `aplay -L` to get more information about the PCM devices defined on the system.
```
$ aplay -L
null
    Discard all samples (playback) or generate zero samples (capture)
default:CARD=ALSA
    bcm2835 ALSA, bcm2835 ALSA
    Default Audio Device
sysdefault:CARD=ALSA
    bcm2835 ALSA, bcm2835 ALSA
    Default Audio Device
dmix:CARD=ALSA,DEV=0
    bcm2835 ALSA, bcm2835 ALSA
    Direct sample mixing device
dmix:CARD=ALSA,DEV=1
    bcm2835 ALSA, bcm2835 IEC958/HDMI
    Direct sample mixing device
dsnoop:CARD=ALSA,DEV=0
    bcm2835 ALSA, bcm2835 ALSA
    Direct sample snooping device
dsnoop:CARD=ALSA,DEV=1
    bcm2835 ALSA, bcm2835 IEC958/HDMI
    Direct sample snooping device
hw:CARD=ALSA,DEV=0
    bcm2835 ALSA, bcm2835 ALSA
    Direct hardware device without any conversions
hw:CARD=ALSA,DEV=1
    bcm2835 ALSA, bcm2835 IEC958/HDMI
    Direct hardware device without any conversions
plughw:CARD=ALSA,DEV=0
    bcm2835 ALSA, bcm2835 ALSA
    Hardware device with all software conversions
plughw:CARD=ALSA,DEV=1
    bcm2835 ALSA, bcm2835 IEC958/HDMI
    Hardware device with all software conversions
default:CARD=IQaudIODAC
    IQaudIODAC, 
    Default Audio Device
sysdefault:CARD=IQaudIODAC
    IQaudIODAC, 
    Default Audio Device
dmix:CARD=IQaudIODAC,DEV=0
    IQaudIODAC, 
    Direct sample mixing device
dsnoop:CARD=IQaudIODAC,DEV=0
    IQaudIODAC, 
    Direct sample snooping device
hw:CARD=IQaudIODAC,DEV=0
    IQaudIODAC, 
    Direct hardware device without any conversions
plughw:CARD=IQaudIODAC,DEV=0
    IQaudIODAC, 
    Hardware device with all software conversions
```
For the server configuration, we will use:
```
audio {
    nickname = "Computer"
    type = "alsa"
    card="hw:1"
    # mixer=TBD
    # mixer_device=TBD
}
```

## Mixer name

Once you have the card number (determined from `aplay -l`) we can inspect/confirm the name of the mixer (it may NOT be `PCM` as expected by the server).  In this example, the card `1` is of interest and thus we use `-c 1` with the following command:
```
$ amixer -c 1 
Simple mixer control 'DSP Program',0
  Capabilities: enum
  Items: 'FIR interpolation with de-emphasis' 'Low latency IIR with de-emphasis' 'High attenuation with de-emphasis' 'Fixed process flow' 'Ringing-less low latency FIR'
  Item0: 'Ringing-less low latency FIR'
Simple mixer control 'Analogue',0
  Capabilities: pvolume
  Playback channels: Front Left - Front Right
  Limits: Playback 0 - 1
  Mono:
  Front Left: Playback 1 [100%] [0.00dB]
  Front Right: Playback 1 [100%] [0.00dB]
Simple mixer control 'Analogue Playback Boost',0
  Capabilities: volume
  Playback channels: Front Left - Front Right
  Capture channels: Front Left - Front Right
  Limits: 0 - 1
  Front Left: 0 [0%] [0.00dB]
  Front Right: 0 [0%] [0.00dB]
...
```

This card has multiple controls but we want the first mixer listed with a `pvolume` capability - in this case that mixer value required for the server configuration is called `Analogue`.

For the server configuration, we will use:
```
audio {
    nickname = "Computer"
    type = "alsa"
    card="hw:1"
    mixer="Analogue"
    # mixer_device=TBD
}
```

## Mixer device
This is the name of the underlying physical device used for the mixer - it is typically the same value as the value of `card` in which case a value is not required by the server configuration.  An example of when you want to change explicitly configure this is if you need to use a `dmix` device (see below).


## Handling Devices that cannot concurrently play multiple audio streams 

Some devices such as various RPI DAC boards (IQaudio DAC, Allo Boss DAC...) cannot have multiple streams openned at the same time/cannot play multiple sound files at the same time. This results in `Device or resource busy` errors.  You can confirm if your sound card has this problem by using the example below once have determined the names/cards information as above.

Using our `hw:1` device we try:

```
# generate some audio
$ sox -n -c 2 -r 44100 -b 16 -C 128 /tmp/sine441.wav synth 30 sin 500-100 fade h 0.2 30 0.2

# attempt to play 2 files at the same time
$ aplay -v -Dhw:1 /tmp/sine441.wav &
Playing WAVE '/tmp/sine441.wav' : Signed 16 bit Little Endian, Rate 44100 Hz, Stereo
Hardware PCM card 1 'IQaudIODAC' device 0 subdevice 0
Its setup is:
  stream       : PLAYBACK
  access       : RW_INTERLEAVED
  format       : S16_LE
  subformat    : STD
  channels     : 2
  rate         : 44100
  exact rate   : 44100 (44100/1)
  msbits       : 16
  buffer_size  : 22052
  period_size  : 5513
  period_time  : 125011
  tstamp_mode  : NONE
  tstamp_type  : MONOTONIC
  period_step  : 1
  avail_min    : 5513
  period_event : 0
  start_threshold  : 22052
  stop_threshold   : 22052
  silence_threshold: 0
  silence_size : 0
  boundary     : 1445199872
  appl_ptr     : 0
  hw_ptr       : 0
$ aplay -v -Dhw:1 /tmp/sine441.wav
aplay: main:788: audio open error: Device or resource busy
```

In this instance this device cannot open multiple streams - `forked-daapd` can handle this situation transparently with some audio being truncated from the end of the current file as the server prepares to play the following track. If this handling is causing you problems you may wish to use [ALSA's `dmix` functionally](https://www.alsa-project.org/main/index.php/Asoundrc#Software_mixing) which provides a software mixing module. We will need to define a `dmix` component and configure the server to use that as it's sound card.

The downside to the `dmix` approach will be the need to fix a samplerate (48000 being the default) for this software mixing module meaning any files that have a mismatched samplerate will be resampled.

## ALSA dmix configuration/setup

A `dmix` device can be defined in `/etc/asound.conf` or `~/.asoundrc`.  We will need to know the underlying physical soundcard to be used: in our examples above, `hw:1,0` / `card 1, device 0` representing our IQaudIODAC as per output of `aplay -l`.  We also take the `buffer_size` and `period_size` from the output of playing a sound file via `aplay -v`.

```
# use 'dac' as the name of the device: "aplay -Ddac ...."
pcm.!dac {
    type plug
    slave.pcm "dmixer"
    hint.description "IQAudio DAC s/w dmix enabled device"
}

pcm.dmixer  {
    type dmix
    ipc_key 1024             # need to be uniq value
    ipc_key_add_uid false    # multiple concurrent different users
    ipc_perm 0666            # multi-user sharing permissions

    slave {
	pcm "hw:1,0"         # points at the underlying device - could also simply be hw:1
	period_time 0
	period_size 4096     # from the output of aplay -v
	buffer_size 22052    # from the output of aplay -v
	rate 44100           # locked in sample rate for resampling on dmix device
    }
    hint.description "IQAudio DAC s/w dmix device"
}

ctl.dmixer {
    type hw
    card 1                  # underlying device
    device 0
}

```
Running `aplay -L` we will see our newly defined devices `dac` and `dmixer`
```
$ aplay -L
null
    Discard all samples (playback) or generate zero samples (capture)
dac
    IQAudio DAC s/w dmix enabled device
dmixer
    IQAudio DAC s/w dmix device
default:CARD=ALSA
    bcm2835 ALSA, bcm2835 ALSA
    Default Audio Device
...
```
At this point we are able to rerun the concurrent `aplay` commands (adding `-Ddac` to specify the playback device to use) to verify ALSA configuration.

If there is only one card on the machine you may use `pcm.!default` instead of `pcm.!dac` - there is less configuration to be done later since many ALSA applications will use the device called `default` by default.  Furthermore on RPI you can explicitly disable the onboard sound card to leave us with only the IQaudIODAC board enabled (won't affect HDMI sound output) by commenting out `#dtparam=audio=on` in `/boot/config.txt` and rebooting.

### forked-daapd config with dmix

We will use the newly defined card named `dac` which uses the underlying `hw:1` device as per `/etc/asound.conf` or `~/.asoundrc` configuration.  Note that the `mixer_device` is now required and must refer to the real device (see the `slave.pcm` value) and not the named device (ie `dac`) that we created and are using for the `card` configuration value.

For the final server configuration, we will use:
```
audio {
    nickname = "Computer"
    type = "alsa"
    card="dac"
    mixer="Analogue"
    mixer_device="hw:1"
}
```

## Multiple devices

If your machine has multiple physical devices like our Raspberry Pi example above (the DAC hat and the onboard headphone jack), we can make all these devices available to `forked-daapd` using seperate `alsa { .. }` sections.

NB: When introducing `alsa { .. }` section(s) the ALSA specific configuration in the `audio { .. }` section will be ignored.

For example:
```
audio {
    type = "alsa"
}

alsa "dac" {
    mixer="Analogue"
    mixer_device="hw:1"
}

alsa "hw:0,0" {
    nickname = "headphones"
    mixer = "PCM"
    mixer_device = "hw:0"
}

```
