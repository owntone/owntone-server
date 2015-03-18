#!/bin/sh
# Credit thorsteneckel who made the how-to that is the basis for this
# script, see https://gist.github.com/thorsteneckel/c0610fb415c8d0486bce

echo "This script will install forked-daapd in FreeBSD 10.1. The script is not"
echo "very polished, so you might want to look through it before running it."
read -p "Continue? [y/N] " yn
if [ "$yn" != "y" ]; then
	exit
fi

DEPS="gmake autoconf automake libtool gettext gperf glib pkgconf wget git \
     ffmpeg libconfuse libevent2 mxml libgcrypt libunistring libiconv \
     libplist avahi sqlite3"
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

WORKDIR=~/forked-daapd_build
CONFIG=/usr/local/etc/forked-daapd.conf
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

read -p "Should the script install antlr and libantlr3c? [y/N] " yn
if [ "$yn" = "y" ]; then
	read -p "Should the script build libantlr3c for 64 bit? [Y/n] " yn
	if [ "$yn" != "n" ]; then
		ENABLE64BIT="--enable-64bit"
	fi

	wget --no-check-certificate https://github.com/antlr/website-antlr3/raw/gh-pages/download/antlr-3.4-complete.jar
	wget --no-check-certificate https://github.com/antlr/website-antlr3/raw/gh-pages/download/C/libantlr3c-3.4.tar.gz

	sudo install antlr-3.4-complete.jar /usr/local/share/java
	printf "#!/bin/sh
export CLASSPATH
CLASSPATH=\$CLASSPATH:/usr/local/share/java/antlr-3.4-complete.jar:/usr/local/share/java
/usr/local/bin/java org.antlr.Tool \$*
" > antlr3
	sudo install --mode=755 antlr3 /usr/local/bin

	tar xzf libantlr3c-3.4.tar.gz
	cd libantlr3c-3.4
	./configure $ENABLE64BIT && gmake && sudo gmake install
	cd $WORKDIR
fi

read -p "Should the script build forked-daapd? [y/N] " yn
if [ "$yn" = "y" ]; then
	git clone https://github.com/ejurgensen/forked-daapd.git
	cd forked-daapd

	#Cleanup in case this is a re-run
	gmake clean
	git clean -f
	autoreconf -vi

#These should no longer be required, but if you run into trouble you can try enabling them
#export CC=cc
#export LIBUNISTRING_CFLAGS=-I/usr/include
#export LIBUNISTRING_LIBS=-L/usr/lib
#export ZLIB_CFLAGS=-I/usr/include
#export ZLIB_LIBS=-L/usr/lib

	export CFLAGS="-march=native -g -I/usr/local/include -I/usr/include"
	export LDFLAGS="-L/usr/local/lib -L/usr/lib"
	./configure --build=i386-portbld-freebsd10.1 && gmake

	read -p "Should the script install forked-daapd and add service startup scripts? [y/N] " yn
	if [ "$yn" = "y" ]; then
		if [ -f $CONFIG ]; then
			echo "Backing up old config file to $CONFIG.bak"
			sudo cp "$CONFIG" "$CONFIG.bak"
		fi
		sudo gmake install

		sudo sed -i -- 's/\/var\/cache/\/usr\/local\/var\/cache/g' $CONFIG
		# Setup user and startup scripts
		echo "daapd::::::forked-daapd:/nonexistent:/usr/sbin/nologin:" | sudo adduser -w no -D -f -
		sudo chown -R daapd:daapd /usr/local/var/cache/forked-daapd
		if [ ! -f scripts/freebsd_start_10.1.sh ]; then
			echo "Could not find FreeBSD startup script"
			exit
		fi
		sudo install --mode=755 scripts/freebsd_start_10.1.sh /usr/local/etc/rc.d/forked-daapd

		service forked-daapd enabled
		if [ $? -ne 0 ]; then
			sudo sh -c 'echo "forked_daapd_enable=\"YES\"" >> /etc/rc.conf'
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

read -p "Should the script (re)start forked-daapd and display the log output? [y/N] " yn
if [ "$yn" = "y" ]; then
	sudo service forked-daapd restart
	tail -f /var/log/forked-daapd.log
fi
