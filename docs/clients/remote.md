# Using Remote

Remote gets a list of output devices from the server; this list includes any
and all devices on the network we know of that advertise AirPlay: AirPort
Express, Apple TV, ... It also includes the local audio output, that is, the
sound card on the server (even if there is no soundcard).

OwnTone remembers your selection and the individual volume for each
output device; selected devices will be automatically re-selected, except if
they return online during playback.

## Pairing

 1. Open the [web interface](http://owntone.local:3689)
 2. Start Remote, go to Settings, Add Library
 3. Enter the pair code in the web interface (update the page with F5 if it does
    not automatically pick up the pairing request)

If Remote doesn't connect to OwnTone after you entered the pairing code
something went wrong. Check the log file to see the error message. Here are
some common reasons:

- You did not enter the correct pairing code

    You will see an error in the log about pairing failure with a HTTP response code
    that is *not* 0.

    Solution: Try again.

- No response from Remote, possibly a network issue

    If you see an error in the log with either:

     - a HTTP response code that is 0
     - "Empty pairing request callback"

    it means that OwnTone could not establish a connection to Remote. This 
    might be a network issue, your router may not be allowing multicast between the
    Remote device and the host OwnTone is running on.

    Solution 1: Sometimes it resolves the issue if you force Remote to quit, restart
    it and do the pairing proces again. Another trick is to establish some other
    connection (eg SSH) from the iPod/iPhone/iPad to the host.

    Solution 2: Check your router settings if you can whitelist multicast addresses
    under IGMP settings. For Apple Bonjour, setting a multicast address of
    224.0.0.251 and a netmask of 255.255.255.255 should work.

- Otherwise try using avahi-browse for troubleshooting:

     - in a terminal, run `avahi-browse -r -k _touch-remote._tcp`
     - start Remote, goto Settings, Add Library
     - after a couple seconds at most, you should get something similar to this:

    ```
    + ath0 IPv4 59eff13ea2f98dbbef6c162f9df71b784a3ef9a3      _touch-remote._tcp   local
    = ath0 IPv4 59eff13ea2f98dbbef6c162f9df71b784a3ef9a3      _touch-remote._tcp   local
       hostname = [Foobar.local]
       address = [192.168.1.1]
       port = [49160]
       txt = ["DvTy=iPod touch" "RemN=Remote" "txtvers=1" "RemV=10000" "Pair=FAEA410630AEC05E" "DvNm=Foobar"]
    ```

    Hit Ctrl-C to terminate avahi-browse.

- To check for network issues you can try to connect to address and port with telnet.
