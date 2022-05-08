# AirPlay devices/speakers

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
