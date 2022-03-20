# Spotify

OwnTone has built-in support for playback of the tracks in your Spotify library.

You must have a Spotify premium account. If you normally log into Spotify with
your Facebook account you must first go to Spotify's web site where you can get
the Spotify username and password that matches your account.

You must also make sure that your browser can reach OwnTone's web interface via
the address [http://owntone.local:3689](http://owntone.local:3689). Try it right
now! That is where Spotify's OAuth page will redirect your browser with the
token that OwnTone needs, so it must work. The address is announced by the
server via mDNS, but if that for some reason doesn't work then configure it via
router or .hosts file. You can remove it again after completing the login.

To authorize OwnTone, open the web interface, locate Settings > Online Services
and then click the Authorize button. You will then be sent to Spotify's
authorization service, which will send you back to the web interface after
you have given the authorization.

Spotify no longer automatically notifies clients about library updates, so you
have to trigger updates manually. You can for instance set up a cron job that
runs `/usr/bin/curl http://localhost:3689/api/update`

To logout and remove Spotify tracks + credentials make a request to
[http://[your_server_address_here]:3689/api/spotify-logout](http://[your_server_address_here]:3689/api/spotify-logout).

Limitations:
You will not be able to do any playlist management through OwnTone - use
a Spotify client for that. You also can only listen to your music by letting
OwnTone do the playback - so that means you can't stream to DAAP clients (e.g.
iTunes) and RSP clients.

## Via librespot/spocon

You can also use OwnTone with one of the various incarnations of
[librespot](https://github.com/librespot-org/librespot). This adds librespot as
a selectable metaspeaker in Spotify's client, and when you start playback,
librespot can be configured to start writing audio to a pipe that you have added
to your library. This will be detected by OwnTone that then starts playback.
You can also have a pipe for metadata and playback events, e.g. volume changes.

The easiest way of accomplishing this may be with [Spocon](https://github.com/spocon/spocon),
since it requires minimal configuration. After installing, create two pipes
(with mkfifo) and set the configuration in the player section:

```
# Audio output device (MIXER, PIPE, STDOUT)
output = "PIPE"
# Output raw (signed) PCM to this file (`player.output` must be PIPE)
pipe = "/srv/music/spotify"
# Output metadata in Shairport Sync format (https://github.com/mikebrady/shairport-sync-metadata-reader)
metadataPipe = "/srv/music/spotify.metadata"
```

## Via libspotify

This method is being deprecated, but is still available if the server was built
with it, libspotify is installed and `use_libspotify` is enabled in the config
file. Please consult [previous README versions](#references) for details on
using libspotify.
