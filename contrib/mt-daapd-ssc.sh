#!/bin/sh
#
# script to facilitate server-side transcoding of ogg files
# Ron Pedde (ron@pedde.com)
#
# Usage: mt-daapd-ssc.sh <filename> <offset>
#
# This is not as flexible as Timo's transcoding script, but it works
# without perl, making it more suitable for the NSLU2.
#

ogg_file() {
    oggdec --quiet -o - $1 | dd bs=1 ibs=1024 obs=1024 skip=$OFFSET 2>/dev/null
}


OFFSET=0

if [ "$2" == "" ]; then
    OFFSET=0
else
    OFFSET=$2
fi


if ( echo $1 | grep -i "\.ogg$" > /dev/null 2>&1 ); then
    ogg_file $1 $OFFSET
fi

