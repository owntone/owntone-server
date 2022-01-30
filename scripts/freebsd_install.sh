#!/bin/sh
# Credit thorsteneckel who made the how-to that is the basis for this
# script, see https://gist.github.com/thorsteneckel/c0610fb415c8d0486bce

echo "This script will install OwnTone in FreeBSD. The script is not very polished,"
echo "so you might want to look through it before running it."
read -p "Continue? [y/N] " yn
if [ "$yn" != "y" ]; then
	exit
fi

DEPS="gmake autoconf automake libtool gettext gperf glib pkgconf wget git \
     ffmpeg libconfuse libevent mxml libgcrypt libunistring libiconv curl \
     libplist libinotify avahi sqlite3 alsa-lib libsodium json-c libwebsockets
     protobuf-c bison flex"
echo "The script can install the following dependency packages for you:"
echo $DEPS
read -p "Should the script install these packages? [y/N] " yn
if [ "$yn" = "y" ]; then
	sudo pkg install $DEPS;
fi

JRE="openjdk8-jre"
read -p "Should the script install $JRE for you? [y/N] " yn
if [ "$yn" = "y" ]; then
	sudo pkg install $JRE;
	read -p "Should the script add the mount points to /etc/fstab that $JRE requests? [y/N] " yn
	if [ "$yn" = "y" ]; then
		sudo sh -c 'echo "fdesc	/dev/fd	fdescfs	rw	0	0" >> /etc/fstab'
		sudo sh -c 'echo "proc	/proc	procfs	rw	0	0" >> /etc/fstab'
		sudo mount /dev/fd
		sudo mount /proc
	fi
fi

WORKDIR=~/owntone_build
CONFIG=/usr/local/etc/owntone.conf
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
	autoreconf -vi

#These should no longer be required, but if you run into trouble you can try enabling them
#export CC=cc
#export LIBUNISTRING_CFLAGS=-I/usr/include
#export LIBUNISTRING_LIBS="-L/usr/lib -lunistring"
#export ZLIB_CFLAGS=-I/usr/include
#export ZLIB_LIBS="-L/usr/lib -lz"

	export CFLAGS="-march=native -g -I/usr/local/include -I/usr/include"
	export LDFLAGS="-L/usr/local/lib -L/usr/lib"
	./configure --disable-install-systemd && gmake

	read -p "Should the script install owntone and add service startup scripts? [y/N] " yn
	if [ "$yn" = "y" ]; then
		sudo gmake install

		sudo sed -i -- 's/\/var\/cache/\/usr\/local\/var\/cache/g' $CONFIG
		# Setup user and startup scripts
		echo "owntone::::::owntone:/nonexistent:/usr/sbin/nologin:" | sudo adduser -w no -D -f -
		sudo chown -R owntone:owntone /usr/local/var/cache/owntone
		if [ ! -f scripts/freebsd_start.sh ]; then
			echo "Could not find FreeBSD startup script"
			exit
		fi
		sudo install -m 755 scripts/freebsd_start.sh /usr/local/etc/rc.d/owntone

		service owntone enabled
		if [ $? -ne 0 ]; then
			sudo sh -c 'echo "owntone_enable=\"YES\"" >> /etc/rc.conf'
		fi
	fi

	cd $WORKDIR
fi

read -p "Should the script enable and start dbus and avahi-daemon? [y/N] " yn
if [ "$yn" = "y" ]; then
	service dbus enabled
	if [ $? -ne 0 ]; then
		sudo sh -c 'echo "dbus_enable=\"YES\"" >> /etc/rc.conf'
	fi
	sudo service dbus start

	service avahi-daemon enabled
	if [ $? -ne 0 ]; then
		sudo sh -c 'echo "avahi_daemon_enable=\"YES\"" >> /etc/rc.conf'
	fi
	sudo service avahi-daemon start
fi

read -p "Should the script (re)start owntone and display the log output? [y/N] " yn
if [ "$yn" = "y" ]; then
	sudo service owntone restart
	tail -f /usr/local/var/log/owntone.log
fi
