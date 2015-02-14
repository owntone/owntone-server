#!/bin/sh

WORKDIR=~/antlr34.tmp
echo "This script will install antlr 3.4 (and matching libantlr) on your computer."
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

read -p "Should the script download and build antlr and libantlr3c? [Y/n] " yn
if [ "$yn" = "n" ]; then
	exit
fi
read -p "Should the script install with prefix /usr or /usr/local? [U/l] " yn
if [ "$yn" = "l" ]; then
	PREFIX=/usr/local
else
	PREFIX=/usr
fi
wget --no-check-certificate https://github.com/antlr/website-antlr3/raw/gh-pages/download/antlr-3.4-complete.jar
wget --no-check-certificate https://github.com/antlr/website-antlr3/raw/gh-pages/download/C/libantlr3c-3.4.tar.gz
tar xzf libantlr3c-3.4.tar.gz
cd libantlr3c-3.4
./configure --disable-abiflags --prefix=$PREFIX && make && sudo make install
cd $WORKDIR

sudo mkdir -p "$PREFIX/share/java"
sudo mv antlr-3.4-complete.jar "$PREFIX/share/java"
printf "#!/bin/sh
export CLASSPATH
CLASSPATH=\$CLASSPATH:$PREFIX/share/java/antlr-3.4-complete.jar:$PREFIX/share/java
/usr/bin/java org.antlr.Tool \$*
" > antlr3
chmod a+x antlr3
sudo mv antlr3 "$PREFIX/bin"

