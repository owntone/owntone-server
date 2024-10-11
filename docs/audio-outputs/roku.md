# Roku devices/speakers

OwnTone can stream audio to classic RSP/RCP-based devices like Roku Soundbridge
M1001 and M2000.

If the source file is in a non-supported format, like flac, OwnTone will
transcode to wav. Transmitting wav requires some bandwidth and the legacy
network interfaces of these devices may struggle with that. If so, you can
change the transcoding format for the speaker to alac via the [JSON API](../json-api.md#change-an-output).
