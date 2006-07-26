#!/bin/sh
#
# script to facilitate server-side transcoding of ogg files
#
#
# Usage: mt-daapd-ssc.sh <filename> <offset> <length in seconds>
#
# You may need to fix these paths:
#

WAVSTREAMER=./wavstreamer
ALAC=./alac
OGGDEC=oggdec
FLAC=flac

alac_file() {
    $ALAC "$FILE" | $WAVSTREAMER -o $OFFSET $FORGELEN
}

ogg_file() {
    $OGGDEC --quiet -o - "$FILE" | $WAVSTREAMER -o $OFFSET $FORGELEN
}


flac_file() {
    $FLAC --silent --decode --stdout "$FILE" | $WAVSTREAMER -o $OFFSET $FORGELEN
}

FILE=$1
OFFSET=0

if [ "$2" == "" ]; then
    OFFSET=0
else
    OFFSET=$2
fi


if [ "$3" != "" ]; then
    FORGELEN="-l $3"
fi

if ( echo $1 | grep -i "\.ogg$" > /dev/null 2>&1 ); then
    ogg_file
    exit;
fi

if ( echo $1 | grep -i "\.flac$" > /dev/null 2>&1 ); then
    flac_file
    exit;
fi

if ( echo $1 | grep -i "\.m4a$" > /dev/null 2>&1 ); then
    alac_file
    exit;
fi

#
# here you could cat a generic "error" wav...
#
#
# cat /path/to/error.wav
#
