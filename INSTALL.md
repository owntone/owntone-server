# Installation instructions for OwnTone

This document contains instructions for installing OwnTone from the git tree. If
you just want to install from a release tarball, you don't need the build tools
(git, autotools, autoconf, automake, gawk, gperf, gettext, bison and flex), and
you can skip the autoreconf step.

The source for this version of OwnTone can be found here:
[owntone/owntone-server](https://github.com/owntone/owntone-server.git)

## Quick version for Raspberry Pi OS

See the instructions here:
[OwnTone server (iTunes server) -
Raspberry Pi Forums](http://www.raspberrypi.org/phpBB3/viewtopic.php?t=49928)

## Quick version for Debian/Ubuntu users

If you are the lucky kind, this should get you all the required tools and
libraries:

```bash
sudo apt-get install \
  build-essential git autotools-dev autoconf automake libtool gettext gawk \
  gperf bison flex libconfuse-dev libunistring-dev libsqlite3-dev \
  libavcodec-dev libavformat-dev libavfilter-dev libswscale-dev libavutil-dev \
  libasound2-dev libmxml-dev libgcrypt20-dev libavahi-client-dev zlib1g-dev \
  libevent-dev libplist-dev libsodium-dev libjson-c-dev libwebsockets-dev \
  libcurl4-openssl-dev libprotobuf-c-dev
```

Note that OwnTone will also work with other versions and flavours of
libgcrypt and libcurl, so the above are just suggestions.

The following features require extra packages, and that you add a configure
argument when you run ./configure:

 Feature              | Configure argument       | Packages
 ---------------------|--------------------------|-------------------------------------
 Chromecast           | `--enable-chromecast`    | libgnutls*-dev
 Spotify (libspotify) | `--enable-libspotify`    | libspotify-dev
 Pulseaudio           | `--with-pulseaudio`      | libpulse-dev

These features can be disabled saving you package dependencies:

 Feature              | Configure argument       | Packages
 ---------------------|--------------------------|-------------------------------------
 Spotify (built-in)   | `--disable-spotify`      | libprotobuf-c-dev
 Player web UI        | `--disable-webinterface` | libwebsockets-dev
 Live web UI          | `--without-libwebsockets`| libwebsockets-dev

Then run the following (adding configure arguments for optional features):

```bash
git clone https://github.com/owntone/owntone-server.git
cd owntone-server
autoreconf -i
./configure --prefix=/usr --sysconfdir=/etc --localstatedir=/var --enable-install-user
make
sudo make install
```

Using `--enable-install-user` means that `make install` will also add system
user and group for owntone.

With the above configure arguments, a systemd service file will be installed to
`/etc/systemd/system/owntone.service` so that the server will start on boot.
Use `--disable-install-systemd` if you don't want that.

Now edit `/etc/owntone.conf`. Note the guide at the top highlighting which
settings that normally require modification.

Start the server with `sudo systemctl start owntone` and check that it is
running with `sudo systemctl status owntone`.

See the [README.md](README.md) for usage information.

## Quick version for Fedora

If you haven't already enabled the free RPM fusion packages do that, since you
will need ffmpeg. You can google how to do that. Then run:

```bash
sudo yum install \
  git automake autoconf gettext-devel gperf gawk libtool bison flex \
  sqlite-devel libconfuse-devel libunistring-devel mxml-devel libevent-devel \
  avahi-devel libgcrypt-devel zlib-devel alsa-lib-devel ffmpeg-devel \
  libplist-devel libsodium-devel json-c-devel libwebsockets-devel \
  libcurl-devel protobuf-c-devel
```

Clone the OwnTone repo:

```bash
git clone https://github.com/owntone/owntone-server.git
cd owntone-server
```

Then run the following:

```bash
autoreconf -i
./configure --prefix=/usr --sysconfdir=/etc --localstatedir=/var --enable-install-user
make
sudo make install
```

Using `--enable-install-user` means that `make install` will also add system
user and group for owntone.

With the above configure arguments, a systemd service file will be installed to
`/etc/systemd/system/owntone.service` so that the server will start on boot.
Use `--disable-install-systemd` if you don't want that.

Now edit `/etc/owntone.conf`. Note the guide at the top highlighting which
settings that normally require modification.

Start the server with `sudo systemctl start owntone` and check that it is
running with `sudo systemctl status owntone`.

See the [README.md](README.md) for usage information.

## Quick version for FreeBSD

There is a script in the 'scripts' folder that will at least attempt to do all
the work for you. And should the script not work for you, you can still look
through it and use it as an installation guide.

## Quick version for macOS (using Homebrew)

This workflow file used for building OwnTone via Github actions includes
all the steps that you need to execute:
[.github/workflows/macos.yml](.github/workflows/macos.yml)

## "Quick" version for macOS (using macports)

Caution:
1) this approach may be out of date, consider using the Homebrew method above
since it is continuously tested.
2) macports requires many downloads and lots of time to install (and sometimes
build) ports... you'll want a decent network connection and some patience!

Install macports (which requires Xcode):
  https://www.macports.org/install.php

```bash
sudo port install \
  autoconf automake libtool pkgconfig git gperf bison flex libgcrypt \
  libunistring libconfuse ffmpeg libevent json-c libwebsockets curl \
  libplist libsodium protobuf-c
```

Download, configure, build and install the Mini-XML library:
  http://www.msweet.org/projects.php/Mini-XML

Download, configure, build and install the libinotify library:
  https://github.com/libinotify-kqueue/libinotify-kqueue

Add the following to `.bashrc`:

```bash
# add /usr/local to pkg-config path
export PKG_CONFIG_PATH=/usr/local/lib/pkgconfig:/opt/local/lib/pkgconfig
# libunistring doesn't support pkg-config, set overrides
export LIBUNISTRING_CFLAGS=-I/opt/local/include
export LIBUNISTRING_LIBS="-L/opt/local/lib -lunistring"
```

Optional features require the following additional ports:

 Feature             | Configure argument       | Ports
 --------------------|--------------------------|-------------------
 Chromecast          | `--enable-chromecast`    | gnutls
 Pulseaudio          | `--with-pulseaudio`      | pulseaudio

Clone the OwnTone repo:

```bash
git clone https://github.com/owntone/owntone-server.git
cd owntone-server
```

Finally, configure, build and install, adding configure arguments for
optional features:

```bash
autoreconf -i
./configure
make
sudo make install
```

Note: if for some reason you've installed the avahi port, you need to
add `--without-avahi` to configure above.

Edit `/usr/local/etc/owntone.conf` and change the `uid` to a nice
system daemon (eg: unknown), and run the following:

```bash
sudo mkdir -p /usr/local/var/run
sudo mkdir -p /usr/local/var/log # or change logfile in conf
sudo chown unknown /usr/local/var/cache/owntone # or change conf
```

Run OwnTone:

```bash
sudo /usr/local/sbin/owntone
```

Verify it's running (you need to <kbd>Ctrl</kbd>+<kbd>C</kbd> to stop
dns-sd):

```bash
dns-sd -B _daap._tcp
```

## Long version - requirements

Required tools:

- autotools: autoconf 2.63+, automake 1.10+, libtool 2.2. Run `autoreconf -i`
  at the top of the source tree to generate the build system.
- gettext: libunistring requires iconv and gettext provides the autotools
  macro definitions for iconv.
- gperf
- bison 3.0+ (yacc is not sufficient)
- flex (lex is not sufficient)

Libraries:

- Avahi client libraries (avahi-client), 0.6.24 minimum  
  from <http://avahi.org/>
- sqlite3 3.5.0+ with unlock notify API enabled (read below)  
  from <http://sqlite.org/download.html>
- ffmpeg (libav)
  from <http://ffmpeg.org/>
- libconfuse  
  from <http://www.nongnu.org/confuse/>
- libevent 2.0+ (best with 2.1.4+)  
  from <http://libevent.org/>
- MiniXML (aka mxml or libmxml)  
  from <http://minixml.org/software.php>
- gcrypt 1.2.0+  
  from <http://gnupg.org/download/index.en.html#libgcrypt>
- zlib  
  from <http://zlib.net/>
- libunistring 0.9.3+  
  from <http://www.gnu.org/software/libunistring/#downloading>
- libjson-c  
  from <https://github.com/json-c/json-c/wiki>
- libcurl
  from <http://curl.haxx.se/libcurl/>
- libplist 0.16+
  from <http://github.com/JonathanBeck/libplist/downloads>
- libsodium
  from <https://download.libsodium.org/doc/>
- libprotobuf-c
  from <https://github.com/protobuf-c/protobuf-c/wiki>
- libasound (optional - ALSA local audio)
  often already installed as part of your distro
- libpulse (optional - Pulseaudio local audio)
  from <https://www.freedesktop.org/wiki/Software/PulseAudio/Download/>
- libspotify (optional - Spotify support)
  (deprecated by Spotify)
- libgnutls (optional - Chromecast support)
  from <http://www.gnutls.org/>
- libwebsockets 2.0.2+ (optional - websocket support)
  from <https://libwebsockets.org/>

If using binary packages, remember that you need the development packages to
build OwnTone (usually named -dev or -devel).

sqlite3 needs to be built with support for the unlock notify API; this isn't
always the case in binary packages, so you may need to rebuild sqlite3 to
enable the unlock notify API (you can check for the presence of the
sqlite3_unlock_notify symbol in the sqlite3 library). Refer to the sqlite3
documentation, look for `SQLITE_ENABLE_UNLOCK_NOTIFY`.

## Long version - building and installing

Start by generating the build system by running `autoreconf -i`. This will
generate the configure script and `Makefile.in`.

To display the configure options `run ./configure --help`.

Support for Spotify is optional. Use `--disable-spotify` to disable this feature.
OwnTone supports two ways of integrating with Spotify: Using its own, built-in
integration layer (which is the default), or to use Spotify's deprecated
libspotify. To enable the latter, you must configure with `--enable-libspotify`
and also make sure libspotify's `libspotify/api.h` is installed at compile time.
At runtime, libspotify must be installed, and `use_libspotify` must be enabled
in owntone.conf. OwnTone uses runtime dynamic linking to the libspotify library,
so even though you compiled with `--enable-libspotify`, the executable will
still be able to run on systems without libspotify. If you only want libspotify
integration, you can use `--disable-spotify` and `--enable-libspotify`.

Support for LastFM scrobbling is optional. Use `--enable-lastfm` to enable this
feature.

Support for the MPD protocol is optional. Use `--disable-mpd` to disable this
feature.

Support for Chromecast devices is optional. Use `--enable-chromecast` to enable
this feature.

The player web interface is optional. Use `--disable-webinterface` to disable
this feature.
If enabled, `sudo make install` will install the prebuild html, js, css files.
The prebuild files are:

- `htdocs/index.html`
- `htdocs/player/*`

The source for the player web interface is located under the `web-src` folder and
requires nodejs >= 6.0 to be built. In the `web-src` folder run `npm install` to
install all dependencies for the player web interface. After that run `npm run build`.
This will build the web interface and update the `htdocs` folder.
(See [README_PLAYER_WEBINTERFACE.md](README_PLAYER_WEBINTERFACE.md) for more
informations)

Building with libwebsockets is required if you want the web interface. It will be enabled
if the library is present (with headers). Use `--without-libwebsockets` to disable.

Building with Pulseaudio is optional. It will be enabled if the library is
present (with headers). Use `--without-pulseaudio` to disable.

Recommended build settings:

```bash
./configure --prefix=/usr --sysconfdir=/etc --localstatedir=/var --enable-install-user
```

After configure run the usual make, and if that went well, `sudo make install`.

With the above configure arguments, a systemd service file will be installed to
`/etc/systemd/system/owntone.service` so that the server will start on boot.
Use `--disable-install-systemd` if you don't want that.

Using `--enable-install-user` means that `make install` will also add a system
user and group for owntone.

After installation:

- edit the configuration file, `/etc/owntone.conf`
- make sure the Avahi daemon is installed and running (Debian:
  `apt install avahi-daemon`)

OwnTone will drop privileges to any user you specify in the configuration file
if it's started as root.

This user must have read permission to your library and read/write permissions
to the database location (`$localstatedir/cache/owntone` by default).

