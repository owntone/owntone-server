# Build Instructions

This document contains instructions for building OwnTone from the git tree. If
you just want to build from a release tarball, you don't need the build tools
(git, autotools, autoconf, automake, gawk, gperf, gettext, bison and flex), and
you can skip the autoreconf step.

## Quick Version for Debian/Ubuntu

If you are the lucky kind, this should get you all the required tools and
libraries:

```bash
sudo apt-get install \
  build-essential git autotools-dev autoconf automake libtool gettext gawk \
  gperf bison flex libconfuse-dev libunistring-dev libsqlite3-dev \
  libavcodec-dev libavformat-dev libavfilter-dev libswscale-dev libavutil-dev \
  libasound2-dev libxml2-dev libgcrypt20-dev libavahi-client-dev zlib1g-dev \
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
 PulseAudio           | `--with-pulseaudio`      | libpulse-dev

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

See the [Documentation](getting-started.md) for usage information.

## Quick version for Fedora

If you haven't already enabled the free RPM fusion packages do that, since you
will need ffmpeg. You can google how to do that. Then run:

```bash
sudo dnf install \
  git automake autoconf gettext-devel gperf gawk libtool bison flex \
  sqlite-devel libconfuse-devel libunistring-devel libxml2-devel libevent-devel \
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

See the [Documentation](getting-started.md) for usage information.

## Quick Version for FreeBSD

There is a script in the 'scripts' folder that will at least attempt to do all
the work for you. And should the script not work for you, you can still look
through it and use it as an installation guide.

## Quick Version for macOS Using Homebrew

This workflow file used for building OwnTone via Github actions includes
all the steps that you need to execute:
[.github/workflows/macos.yml](https://github.com/owntone/owntone-server/blob/master/.github/workflows/macos.yml)

## "Quick" Version for macOS Using MacPorts

Caution:

1) this approach may be out of date, consider using the Homebrew method above
since it is continuously tested.
2) MacPorts requires many downloads and lots of time to install (and sometimes
build) ports. You will need a decent network connection and some patience!

Install MacPorts (which requires Xcode): <https://www.macports.org/install.php>

```bash
sudo port install \
  autoconf automake libtool pkgconfig git gperf bison flex libgcrypt \
  libunistring libconfuse ffmpeg libevent json-c libwebsockets curl \
  libplist libsodium protobuf-c libxml2
```

Download, configure, build and install the [libinotify-kqueue library](https://github.com/libinotify-kqueue/libinotify-kqueue)

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
 PulseAudio          | `--with-pulseaudio`      | pulseaudio

Clone the OwnTone repository:

```bash
git clone https://github.com/owntone/owntone-server.git
cd owntone-server
```

Finally, configure, build, install, and add configuration arguments for
optional features:

```bash
autoreconf -i
./configure
make
sudo make install
```

Note: if for some reason you've installed the `avahi` port, you need to
add `--without-avahi` to configure above.

Edit `/usr/local/etc/owntone.conf` and change the `uid` to a proper
system daemon (eg: unknown), and run the following commands:

```bash
sudo mkdir -p /usr/local/var/run
sudo mkdir -p /usr/local/var/log # or change logfile in conf
sudo chown unknown /usr/local/var/cache/owntone # or change conf
```

Run OwnTone:

```bash
sudo /usr/local/sbin/owntone
```

Verify it is running (you need to <kbd>Ctrl</kbd>+<kbd>C</kbd> to stop
dns-sd):

```bash
dns-sd -B _daap._tcp
```

## Long Version - Requirements

Required tools:

- autotools: autoconf 2.63+, automake 1.10+, libtool 2.2. Run `autoreconf -i`
  at the top of the source tree to generate the build system.
- gettext: libunistring requires iconv and gettext provides the autotools
  macro definitions for iconv.
- gperf
- bison 3.0+ (yacc is not sufficient)
- flex (lex is not sufficient)

Libraries:

- [Avahi](https://avahi.org/) client libraries (avahi-client) 0.6.24+
- [SQLite](https://sqlite.org/) 3.5.0+ with the unlock notify API enabled.
  SQLite needs to be built with the support for the unlock notify API; this is not
  always the case in binary packages, so you may need to rebuild SQLite to
  enable the unlock notify API. You can check for the presence of the
  `sqlite3_unlock_notify` symbol in the sqlite library. Refer to the  `SQLITE_ENABLE_UNLOCK_NOTIFY` in the SQLlite documentation.
- [FFmpeg](https://ffmpeg.org/)
- [libconfuse](https://github.com/libconfuse/libconfuse)  
- [libevent](https://libevent.org/) 2.1.4+
- [libxml2](https://gitlab.gnome.org/GNOME/libxml2)  
- [Libgcrypt](https://gnupg.org/software/libgcrypt/) 1.2.0+  
- [zlib](https://zlib.net/)
- [libunistring](https://www.gnu.org/software/libunistring/) 0.9.3+
- [json-c](https://github.com/json-c/json-c/)
- [libcurl](https://curl.se/libcurl/)
- [libplist](https://github.com/JonathanBeck/libplist/) 0.16+
- [libsodium](https://doc.libsodium.org/)
- [protobuf-c](https://github.com/protobuf-c/protobuf-c/)
- [alsa-lib](https://github.com/alsa-project/alsa-lib/) (optional - ALSA local audio)
  often already installed as part of your distro
- [PulseAudio](https://www.freedesktop.org/wiki/Software/PulseAudio/) (optional - PulseAudio local audio)
- [GnuTLS](https://www.gnutls.org/) (optional - Chromecast support)
- [Libwebsockets](https://libwebsockets.org/) 2.0.2+ (optional - websocket support)

Note: If using binary packages, remember that you need the development packages to
build OwnTone (usually suffixed with -dev or -devel).

## Long Version - Building and Installing

Start by generating the build system by running `autoreconf -i`. This will
generate the configure script and `Makefile.in`.

To display the configure options `run ./configure --help`.

Support for Spotify is optional. Use `--disable-spotify` to disable this feature.

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

To serve the web interface locally you can run `npm run serve`, which will make
it reachable at [localhost:3000](http://localhost:3000). The command expects the
server be running at [localhost:3689](http://localhost:3689) and proxies API
calls to this location. If the server is running at a different location you
can use `export VITE_OWNTONE_URL=http://owntone.local:3689`.

Building with libwebsockets is required if you want the web interface.
It will be enabled if the library is present (with headers).
Use `--without-libwebsockets` to disable.

Building with PulseAudio is optional. It will be enabled if the library is
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

## Web source formatting/linting

The source code follows certain formatting conventions for maintainability and
readability. To ensure that the source code follows these conventions,
[Prettier](https://prettier.io/) is used.

The command `npm run format` applies formatting conventions to the source code
based on a preset configuration. Note that a additional configuration is made in
the file `.prettierrc.json`.

To flag programming errors, bugs, stylistic errors and suspicious constructs in
the source code, [ESLint](https://eslint.org) is used.

ESLint has been configured following this [guide](https://vueschool.io/articles/vuejs-tutorials/eslint-and-prettier-with-vite-and-vue-js-3/).

`npm run lint` lints the source code and fixes all automatically fixable errors.

## Non-Priviliged User Version for Development

OwnTone is meant to be run as system wide daemon, but for development purposes
you may want to run it isolated to your regular user.

The following description assumes that you want all runtime data stored in
`$HOME/owntone_data` and the source  in `$HOME/projects/owntone-server`.

Prepare directories for runtime data:

```bash
mkdir -p $HOME/owntone_data/etc
mkdir -p $HOME/owntone_data/media
```

Copy one or more mp3 file to test with to `owntone_data/media`.

Checkout OwnTone and configure build:

```bash
cd $HOME/projects
git clone https://github.com/owntone/owntone-server.git
cd owntone-server
autoreconf -vi
./configure --prefix=$HOME/owntone_data/usr --sysconfdir=$HOME/owntone_data/etc --localstatedir=$HOME/owntone_data/var
```

Build and install runtime:

```bash
make install
```

Edit `owntone_data/etc/owntone.conf`, find the following configuration settings
and set them to these values:

```conf
  uid = ${USER}
  loglevel = "debug"
  # OLD STYLE (still supported)
  # directories = { "${HOME}/owntone_data/media" }
  # NEW STYLE (preferred)
  dirs {
    directory local {
      path = "${HOME}/owntone_data/media"
      use_fs_events = true
    }
  }
```

Run the server:

```bash
./src/owntone -f
```

Note: You can also use the copy of the binary located in `$HOME/owntone_data/usr/sbin`
