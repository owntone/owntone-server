#!/bin/sh
#
# script to facilitate server-side transcoding of ogg files
#
#
# Usage: mt-daapd-ssc.sh <filename> <offset> <length in seconds>
#
# You may need to fix these paths:
#

WAVSTREAMER=wavstreamer
OGGDEC=oggdec
FLAC=flac

ogg_file() {
    $OGGDEC --quiet -o - "$1" | $WAVSTREAMER -o $2 -l $3
}


flac_file() {
    $FLAC --silent --decode --stdout "$1" | $WAVSTREAMER -o $2 -l $3
}

OFFSET=0
FORGELEN=0

if [ "$2" == "" ]; then
    OFFSET=0
else
    OFFSET=$2
fi


if [ "$3" == "" ]; then
    FORGELEN=0
else
    FORGELEN=$3
fi

if ( echo $1 | grep -i "\.ogg$" > /dev/null 2>&1 ); then
    ogg_file $1 $OFFSET $FORGELEN
    exit;
fi

if ( echo $1 | grep -i "\.flac$" > /dev/null 2>&1 ); then
    flac_file $1 $OFFSET $FORGELEN
    exit;
fi

#
# here you could cat a generic "error" wav...
#
#
# cat /path/to/error.wav
#
