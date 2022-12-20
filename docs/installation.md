# How to get and install OwnTone

You can compile and run OwnTone on pretty much any Linux- or BSD-platform. The
instructions are [here](building.md).

Apt repositories, images and precompiled binaries are available for some
platforms. These can save you some work and make it easier to stay up to date:

Platform              | How to get
----------------------|---------------------------------------------------------
RPi w/Raspberry Pi OS | Add OwnTone repository to apt sources, see:<br>[OwnTone server (iTunes server) - Raspberry Pi Forums](http://www.raspberrypi.org/phpBB3/viewtopic.php?t=49928)
Debian/Ubuntu amd64   | Download .deb as [artifact from Github workflow](https://github.com/owntone/owntone-apt/actions)
OpenWrt               | Run `opkg install owntone`
Docker                | See [linuxserver/docker-daapd](https://github.com/linuxserver/docker-daapd)

OwnTone is not in the official Debian repositories due to lack of Debian
maintainer and Debian policy difficulties concerning the web UI, see
[this issue](https://github.com/owntone/owntone-server/issues/552).
