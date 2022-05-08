# Library

The library is scanned in bulk mode at startup, but the server will be available
even while this scan is in progress. You can follow the progress of the scan in
the log file or via the web interface. When the scan is complete you will see
the log message: "Bulk library scan completed in X sec".

The very first scan will take longer than subsequent startup scans, since every
file gets analyzed. At the following startups the server looks for changed files
and only analyzis those.

Updates to the library are reflected in real time after the initial scan, so you
do not need to manually start rescans. The directories are monitored for changes
and rescanned on the fly. Note that if you have your library on a network mount
then real time updating may not work. Read below about what to do in that case.

If you change any of the directory settings in the library section of the
configuration file a rescan is required before the new setting will take effect.
You can do this by using "Update library" from the web interface.

Symlinks are supported and dereferenced, but it is best to use them for
directories only.


## Pipes (for e.g. multiroom with Shairport-sync)

Some programs, like for instance Shairport-sync, can be configured to output
audio to a named pipe. If this pipe is placed in the library, OwnTone will
automatically detect that it is there, and when there is audio being written to
it, playback of the audio will be autostarted (and stopped).

Using this feature, OwnTone can act as an AirPlay multiroom "router": You can
have an AirPlay source (e.g. your iPhone) send audio Shairport-sync, which
forwards it to OwnTone through the pipe, which then plays it on whatever
speakers you have selected (through Remote).

The format of the audio being written to the pipe must be PCM16.

You can also start playback of pipes manually. You will find them in remotes 
listed under "Unknown artist" and "Unknown album". The track title will be the
name of the pipe.

Shairport-sync can write metadata to a pipe, and OwnTone can read this.
This requires that the metadata pipe has the same filename as the audio pipe
plus a ".metadata" suffix. Say Shairport-sync is configured to write audio to
"/foo/bar/pipe", then the metadata pipe should be "/foo/bar/pipe.metadata".


## Libraries on network mounts

Most network filesharing protocols do not offer notifications when the library
is changed. So that means OwnTone cannot update its database in real time.
Instead you can schedule a cron job to update the database.

The first step in doing this is to add two entries to the 'directories'
configuration item in owntone.conf:

```
  directories = { "/some/local/dir", "/your/network/mount/library" }
```

Now you can make a cron job that runs this command:

```
  touch /some/local/dir/trigger.init-rescan
```

When OwnTone detects a file with filename ending .init-rescan it will
perform a bulk scan similar to the startup scan.

Alternatively, you can force a metadata scan of the library even if the
files have not changed by creating a filename ending `.meta-rescan`.

## Supported formats

OwnTone should support pretty much all audio formats. It relies on libav
(or ffmpeg) to extract metadata and decode the files on the fly when the client
doesn't support the format.

Formats are attributed a code, so any new format will need to be explicitely
added. Currently supported:

- MPEG4: mp4a, mp4v
- AAC: alac
- MP3 (and friends): mpeg
- FLAC: flac
- OGG VORBIS: ogg
- Musepack: mpc
- WMA: wma (WMA Pro), wmal (WMA Lossless), wmav (WMA video)
- AIFF: aif
- WAV: wav
- Monkey's audio: ape

## Troubleshooting library issues

If you place a file with the filename ending .full-rescan in your library,
you can trigger a full rescan of your library. This will clear all music and
playlists from OwnTone's database and initiate a fresh bulk scan. Pairing
and speaker information will be kept. Only use this for troubleshooting, it is
not necessary during normal operation.
