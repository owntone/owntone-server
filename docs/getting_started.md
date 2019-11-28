# Getting started

## Installation

### Prebuilt packages

=== "Raspberry Pi OS"

    1. Add repository key

        ``` bash
        wget -q -O - http://www.gyfgafguf.dk/raspbian/forked-daapd.gpg | sudo apt-key add -
        ```

    2. Add APT repository to `/etc/apt/sources.list`:

        For Buster:

        ``` bash
        deb http://www.gyfgafguf.dk/raspbian/forked-daapd/ buster contrib
        ```

        For Stretch:

        ``` bash
        deb http://www.gyfgafguf.dk/raspbian/forked-daapd/ stretch contrib
        ```

    3. Install forked-daapd and its dependencies:

        ``` bash
        sudo apt update
        sudo apt install forked-daapd
        ```

    For further informations and RPi related support visit:
    [Improved forked-daapd (iTunes server) - Raspberry Pi Forums](https://www.raspberrypi.org/forums/viewtopic.php?t=49928)

=== "Debian/Ubuntu"

    !!! warning "Web interface and Spotify"
        The forked-daapd package in Debian and Ubuntu comes __without support
        for Spotify__ ([Issue #34](https://github.com/ejurgensen/forked-daapd/issues/34))
        and __without the web interface__ ([Issue #552](https://github.com/ejurgensen/forked-daapd/issues/552)).

        It is possible to install the web interface manually by copying the files from
        the `htdocs` folder to `/usr/share/forked-daapd/htdocs`.

    ``` bash
    sudo apt install forked-daapd
    ```

### Building from source

Detailed instructions for building forked-daapd from source for different
distributions / operating systems can be found under [Building from Source](install.md).

### Docker images

Community maintained docker images for forked-daapd can be found on [linuxserver/docker-daapd](https://github.com/linuxserver/docker-daapd).

## Setup / configuration

Edit the configuration file (usually `/etc/forked-daapd.conf`) to suit your needs,
e.g.:

1. Add your local music folder to `library.directories`

## First run

After modifying the configuration file restart forked-daapd (usually done with
`sudo systemctl restart forked-daapd.service`).

forked-daapd scans the configured folders which can take a while. You can follow
the progress in the log file:

```bash
tail -f /var/log/forked-daapd.log
```

You can access forked-daapd's web interface by visiting [http://forked-daapd.local:3689](http://forked-daapd.local:3689)
or, if that won't work, by visiting `http://[your_server_address_here]:3689`.

### AirPlay and Chromecast

forked-daapd should automatically discover AirPlay and Chromecast devices on
your network.

If your speaker requires pairing / device verification  (e. g. required by Apple
TV4 with tvOS 10.2) then you can do that in the [web interface](webinterface.md)
through `Settings` > `Remotes & Outputs`:

1. Select the device
2. Enter the PIN that the speaker / device displays

### Pairing Apple Remote

If you want to use a [remote app](remote.md), e.g. Apple Remote:

- Open the web interface
- Open the app on your mobile phone and go to `Settings`, `Add Library`. The app
  will now display a 4 digit pin
- After a few seconds a modal dialog will appear in forked-daapds web interface.
  Enter the pin from the app there.
  The pairing should now complete and your remote app should show your media library
