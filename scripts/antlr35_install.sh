#!/bin/sh

WORKDIR=~/antlr35.tmp

# programs
MAKE=${MAKE-make}
DOWNLOAD="wget --no-check-certificate"
ALTDOWNLOAD="curl -LO"
SUDO=sudo

# source
ANTLR_VERSION=3.5

ANTLR3_SOURCE="https://github.com/antlr/website-antlr3/raw/gh-pages/download"
ANTLR3_JAR="antlr-3.5.2-complete.jar"
ANTLR3_URL="$ANTLR3_SOURCE/$ANTLR3_JAR"

LIBANTLR3C="libantlr3c-3.4"
LIBANTLR3C_SOURCE="https://github.com/antlr/website-antlr3/raw/gh-pages/download/C"
LIBANTLR3C_TAR="${LIBANTLR3C}.tar.gz"
LIBANTLR3C_URL="$LIBANTLR3C_SOURCE/$LIBANTLR3C_TAR"

usage() {
  echo
  echo "This script will download, build and install antlr $ANTLR_VERSION"
  echo "  (and matching libantlrc) on your computer."
  echo
  echo "Usage: ${0##*/} -h | [ -p <prefix> ] [ -y ] [ <build-dir> ]"
  echo
  echo "Parameters:"
  echo "  -h           Show this help"
  echo "  -p <prefix>  Install to prefix (default: choose /usr or /usr/local)"
  echo "  -y           Automatic yes to prompts (run non-interactively with -p)"
  echo "  <build-dir>  Build directory (default: $WORKDIR)"
  exit 0
}

GIVEN_PREFIX=
ALWAYS_YES=
while [ "$1" != "" ]; do
  case $1 in
    -p | --prefix )
      shift
      GIVEN_PREFIX=$1
      ;;
    -y | --yes )
      ALWAYS_YES=1
      ;;
    -h | --help )
      usage
      exit
      ;;
    * )
      echo "Unrecognized option $1 (try -h for usage)"
      exit 1
      ;;
  esac
  shift
done

# override build directory? (support ~ expansion)
[ -n "$1" ] && WORKDIR=$1
ORIG_DIR=`pwd`

err() {
  echo "$*"
  if [ -n "$FILES_EXIST" ]; then
    echo "Files remain in $WORKDIR..."
  else
    cd "$ORIG_DIR"
    rmdir "$WORKDIR"
  fi
  exit 1
}

is_yes() {
  case "$1" in
    [N]*|[n]*) return 1;;
    *) ;;
  esac
  return 0
}

ask_yn() {
  if [ "$ALWAYS_YES" = "1" ]; then
    yn="y"
  else
    read -p "$1" yn
  fi
}

prog_install() {
  ask_yn "Would you like to install into $PREFIX now? [Y/n] "
  if ! is_yes "$yn"; then
    echo "Build left ready to install from $WORKDIR"
    echo "You can re-run the script (eg. as root) to install into"
    echo "  $PREFIX later."
    exit
  fi
  if [ `id -u` -ne 0 ]; then
    ask_yn "Would you like to install with sudo? NOTE: You WILL be asked for your password! [Y/n] "
    if ! is_yes "$yn"; then
      SUDO=
      ask_yn "Continue to install as non-root user? [Y/n] "
      is_yes "$yn" || err "Install cancelled"
    fi
  else
    SUDO=
  fi
  cd $LIBANTLR3C || err "Unable to cd to build libantlr3c build directory!"
  echo "Installing libantlr3c to $PREFIX"
  $SUDO $MAKE install || err "Install of libantlr3c to $PREFIX failed!"

  cd "$ORIG_DIR"
  cd $WORKDIR
  echo "Installing antlr3 to $PREFIX"
  $SUDO mkdir -p "$PREFIX_JAVA" || err "Unable to create $PREFIX_JAVA"
  $SUDO install "$ANTLR3_JAR" "$PREFIX_JAVA" || \
    err "Failed to install antlr3 jar to $PREFIX_JAVA"
  $SUDO mkdir -p "$PREFIX/bin" || err "Unable to create $PREFIX/bin"
  $SUDO install -m 755 antlr3 "$PREFIX/bin" || \
    err "Failed to install antlr3 to $PREFIX/bin"
  echo "Install complete (build remains in $WORKDIR)"
}

echo "This script will download, build and install antlr $ANTLR_VERSION"
echo "  (and matching libantlrc) on your computer."
echo

# check if make works
ISGNU=`$MAKE --version 2>/dev/null | grep "GNU Make"`
if [ -z "$ISGNU" ]; then
  MAKE=gmake
  ISGNU=`$MAKE --version 2>/dev/null | grep "GNU Make"`
fi
[ -z "$ISGNU" ] && err "Unable to locate GNU Make, set \$MAKE to it's location and re-run"

if [ -f "$WORKDIR/install_env" ]; then
  echo "Existing build found in $WORKDIR"
  FILES_EXIST=1
  cd $WORKDIR || err "Unable to cd to '$WORKDIR'"
  . install_env
  [ -n "$PREFIX" ] || err "PREFIX is missing in file 'install_env'"
  if [ -n "$GIVEN_PREFIX" ] && [ "$GIVEN_PREFIX" != "$PREFIX" ]; then
    echo "You must rebuild to install into $GIVEN_PREFIX (current build for $PREFIX)"
    ask_yn "Would you like to rebuild for ${GIVEN_PREFIX}? [Y/n] "
    if is_yes "$yn"; then
      rm -f install_env
      PREFIX=
    else
      ask_yn "Would you like to install to ${PREFIX}? [Y/n] "
      ! is_yes "$yn" && err "Install cancelled"
    fi
  fi
  if [ -n "$PREFIX" ]; then
    PREFIX_JAVA=$PREFIX/share/java
    prog_install
    exit 0
  fi
fi

if [ ! -d "$WORKDIR" ]; then
  ask_yn "Should the script create $WORKDIR and use it for building? [Y/n] "
  is_yes "$yn" || exit
fi

if [ -n "$GIVEN_PREFIX" ]; then
  PREFIX=$GIVEN_PREFIX
else
  read -p "Should the script install with prefix /usr or /usr/local? [U/l] " yn
  if [ "$yn" = "l" ]; then
    PREFIX=/usr/local
  else
    PREFIX=/usr
  fi
fi
PREFIX_JAVA=$PREFIX/share/java

MACHBITS=`getconf LONG_BIT 2>/dev/null`
[ "$MACHBITS" = "64" ] && DEF_AN="[Y/n]" || DEF_AN="[y/N]"
ask_yn "Should the script build libantlr3c for 64 bit? $DEF_AN "
[ -z "$yn" -a "$MACHBITS" != "64" ] && yn=n
is_yes "$yn" && ENABLE64BIT="--enable-64bit"

mkdir -p "$WORKDIR" || err "Error creating $WORKDIR"
# don't quote WORKDIR to catch a WORKDIR that will break the build (eg spaces) 
cd $WORKDIR || err "Unable to cd to '$WORKDIR' (does it include spaces?)"

REMOVE_ON_CANCEL=
cancel_download() {
  echo "removing $REMOVE_ON_CANCEL"
  [ -n "$REMOVE_ON_CANCEL" ] && rm -f "$REMOVE_ON_CANCEL"
  err "Cancelling download..."
}

antlr_download() {
  trap cancel_download SIGINT
  $DOWNLOAD --help >/dev/null 2>&1 || DOWNLOAD=$ALTDOWNLOAD
  $DOWNLOAD --help >/dev/null 2>&1 || {
    echo "Unable to find wget or curl commands to download source,"
    echo "  please install either one and re-try."
    exit 1
  }
  [ "x$1" = "xreset" ] && rm "$ANTLR3_JAR" "$LIBANTLR3C_TAR"
  if [ ! -f "$ANTLR3_JAR" ]; then
    echo
    echo "Downloading antlr from $ANTLR3_URL"
    echo "Ctrl-C to abort..."
    REMOVE_ON_CANCEL=$ANTLR3_JAR
    $DOWNLOAD "$ANTLR3_URL" || err "Download of $ANTLR3_JAR failed!"
    FILES_EXIST=1
  fi
  if [ ! -f "$LIBANTLR3C_TAR" ]; then
    echo
    echo "Downloading libantlr3c from $LIBANTLR3C_URL"
    echo "Ctrl-C to abort..."
    REMOVE_ON_CANCEL=$LIBANTLR3C_TAR
    $DOWNLOAD "$LIBANTLR3C_URL" || err "Download of $LIBANTLR3C_TAR failed!"
    FILES_EXIST=1
  fi
  trap - SIGINT
}

# retrieve the source
if [ -f "$ANTLR3_JAR" -a -f "$LIBANTLR3C_TAR" ]; then
  FILES_EXIST=1
  ask_yn "Files appear to already be downloaded, use them? [Y/n] "
  ! is_yes "$yn" && antlr_download reset
else
  ask_yn "Should the script download and build antlr and libantlr3c? [Y/n] "
  is_yes "$yn" || exit
  antlr_download
fi

# build/install libantlr3c
[ -d "$LIBANTLR3C" ] && rm -rf "$LIBANTLR3C"
tar xzf "$LIBANTLR3C_TAR" || err "Uncompress of $LIBANTLR3C_TAR failed!"
cd $LIBANTLR3C || err "Unable to cd to build $LIBANTLR3C build directory!"
./configure $ENABLE64BIT --prefix=$PREFIX && $MAKE
[ $? -ne 0 ] && err "Build of libantlr3c failed!"

# install antlr3 jar and wrapper
cd "$ORIG_DIR"
cd $WORKDIR
printf "#!/bin/sh
export CLASSPATH
CLASSPATH=\$CLASSPATH:$PREFIX_JAVA/${ANTLR3_JAR}:$PREFIX_JAVA
/usr/bin/java org.antlr.Tool \$*
" > antlr3

# save for later install attempts
echo "PREFIX=$PREFIX" > install_env
echo

prog_install
