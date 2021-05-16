# Spotify

OwnTone has support for playback of the tracks in your Spotify library.

1. Go to the [web interface](http://owntone.local:3689) and check that your
   version of OwnTone was built with Spotify support.
2. You must have a Spotify premium account. If you normally log into Spotify
   with your Facebook account you must first go to Spotify's web site where you
   can get the Spotify username and password that matches your account.
3. Make sure you have `libspotify` installed. Unfortunately, it is no longer
   available from Spotify, and at the time of writing this they have not
   provided an alternative. However, on most Debian-based platforms, you can
   still get it like this:
   - Add the mopidy repository, see [instructions](https://apt.mopidy.com)
   - Install with `apt install libspotify-dev`

Once the above is in order you can login to Spotify via the web interface. The
procedure for logging in to Spotify is a two-step procedure due to the current
state of libspotify, but the web interface makes both steps available to you.

Note that the address [http://owntone.local:3689](http://owntone.local:3689)
must be working on your local network to complete the Spotify OAuth web login.
The address is announced automatically via mDNS, but if that for some reason
doesn't work then configure it via router or .hosts file. You can remove it
again after completing the login. This is needed because the redirect_uri
parameter of the Spotify token request is to this address.

Spotify no longer automatically notifies clients about playlist updates, so you
have to trigger updates manually. You can for instance set up a cron job that
runs `/usr/bin/curl http://localhost:3689/api/update`

OwnTone will not store your password, but will still be able to log you in
automatically afterwards, because libspotify saves a login token. You can
configure the location of your Spotify user data in the configuration file.

To permanently logout and remove Spotify tracks + credentials make a request to
[http://[your_server_address_here]:3689/api/spotify-logout](http://[your_server_address_here]:3689/api/spotify-logout)
and also delete the contents of `/var/cache/owntone/libspotify`.

Limitations:
You will not be able to do any playlist management through OwnTone - use
a Spotify client for that. You also can only listen to your music by letting
OwnTone do the playback - so that means you can't stream to DAAP clients (e.g.
iTunes) and RSP clients.

Alternatives:
If you want OwnTone to be a selectable metaspeaker in Spotify's client, you
can use [librespot](https://github.com/librespot-org/librespot) to write audio
to a pipe in your library. There will be some lag with volume adjustments, and
getting metadata to work also requires extra tinkering.
