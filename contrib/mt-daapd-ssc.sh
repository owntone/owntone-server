#!/opt/bin/bash
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
    if [ $OFFSET -eq 0 ]; then
	    oggdec --quiet -o - "$1"
    else
	    oggdec --quiet -o - "$1" | dd bs=$OFFSET skip=1 2>/dev/null
    fi
}


OFFSET=0

if [ "$2" == "" ]; then
    OFFSET=0
else
    OFFSET=$2
fi

if [ $OFFSET -lt 1024 ]; then
	OFFSET=0
fi

ogg_file $1 $OFFSET

