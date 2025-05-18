#!/bin/sh
# Credit thorsteneckel who made the how-to that is the basis for this
# script, see https://gist.github.com/thorsteneckel/c0610fb415c8d0486bce

USERNAME=owntone
USERGROUP=owntone
SYSCONFDIR=/usr/local/etc
CONFIG="${SYSCONFDIR}/owntone.conf"

SUDO=sudo
if [ "$(id -u)" == "0" ]; then
	SUDO=
fi

echo "This script will install OwnTone in FreeBSD. The script is not very polished,"
echo "so you might want to look through it before running it."
read -p "Continue? [y/N] " yn
if [ "$yn" != "y" ]; then
	exit
fi

DEPS="gmake autoconf automake libtool gettext gperf glib pkgconf wget git \
     ffmpeg libconfuse libevent libxml2 libgcrypt libunistring libiconv curl \
     libplist libinotify avahi sqlite3 alsa-lib libsodium json-c libwebsockets
     protobuf-c bison flex"
echo "The script can install the following dependency packages for you:"
echo $DEPS
read -p "Should the script install these packages? [y/N] " yn
if [ "$yn" = "y" ]; then
	$SUDO pkg install $DEPS;
fi

WORKDIR=~/owntone_build
read -p "Should the script create $WORKDIR and use it for building? [Y/n] " yn
if [ "$yn" = "n" ]; then
	exit
fi
mkdir -p $WORKDIR
if [ ! -d $WORKDIR ]; then
	echo "Error creating $WORKDIR"
	exit
fi
cd $WORKDIR

read -p "Should the script build owntone? [y/N] " yn
if [ "$yn" = "y" ]; then
	git clone https://github.com/owntone/owntone-server.git
	cd owntone-server

	#Cleanup in case this is a re-run
	gmake clean
	git clean -f
	autoreconf -vif

#These should no longer be required, but if you run into trouble you can try enabling them
#export CC=cc
#export LIBUNISTRING_CFLAGS=-I/usr/include
#export LIBUNISTRING_LIBS="-L/usr/lib -lunistring"
#export ZLIB_CFLAGS=-I/usr/include
#export ZLIB_LIBS="-L/usr/lib -lz"

	# some compilers don't support -march=native, so only try it in likely cases
	ARCH=
	UNAME_PROCESSOR=`(uname -p) 2>/dev/null`  || UNAME_PROCESSOR=unknown
	case "$UNAME_PROCESSOR" in
		amd64|x86_64|i686|i386)
			ARCH="-march=native"
	esac

	export CFLAGS="${ARCH} -g -I/usr/local/include -I/usr/include"
	export LDFLAGS="-L/usr/local/lib -L/usr/lib"
	./configure --disable-install-systemd --with-user=$USERNAME --with-group=$USERGROUP --sysconfdir=$SYSCONFDIR && gmake

	read -p "Should the script install owntone and add service startup scripts? [y/N] " yn
	if [ "$yn" = "y" ]; then
		$SUDO gmake install

		$SUDO sed -i -- 's/\/var\/cache/\/usr\/local\/var\/cache/g' $CONFIG
		# Setup user and startup scripts
		if $(id $USERNAME >/dev/null 2>&1); then
		else
			echo "${USERNAME}::::::${USERGROUP}:/nonexistent:/usr/sbin/nologin:" | $SUDO adduser -w no -D -f -
		fi
		
		$SUDO chown -R ${USERNAME}:${USERGROUP} /usr/local/var/cache/owntone
		if [ ! -f scripts/freebsd_start.sh ]; then
			echo "Could not find FreeBSD startup script"
			exit
		fi
		$SUDO install -m 755 scripts/freebsd_start.sh /usr/local/etc/rc.d/owntone

		service owntone enabled
		if [ $? -ne 0 ]; then
			$SUDO sh -c 'echo "owntone_enable=\"YES\"" >> /etc/rc.conf'
		fi
	fi

	cd $WORKDIR
fi

read -p "Should the script enable and start dbus and avahi-daemon? [y/N] " yn
if [ "$yn" = "y" ]; then
	service dbus enabled
	if [ $? -ne 0 ]; then
		$SUDO sh -c 'echo "dbus_enable=\"YES\"" >> /etc/rc.conf'
	fi
	$SUDO service dbus start

	service avahi-daemon enabled
	if [ $? -ne 0 ]; then
		$SUDO sh -c 'echo "avahi_daemon_enable=\"YES\"" >> /etc/rc.conf'
	fi
	$SUDO service avahi-daemon start
fi

read -p "Should the script (re)start owntone and display the log output? [y/N] " yn
if [ "$yn" = "y" ]; then
	$SUDO service owntone restart
	tail -f /usr/local/var/log/owntone.log
fi
