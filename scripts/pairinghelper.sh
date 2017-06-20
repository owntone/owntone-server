#!/bin/sh

# Default config file
conf_path="/etc/forked-daapd.conf"

usage() {
  echo
  echo "Interactive script pair Remote with forked-daapd"
  echo
  echo "Usage: ${0##*/} -h | [ <config-file> ]"
  echo
  echo "Parameters:"
  echo "  -h           Show this help"
  echo " <config-file> Config file (default: $conf_path)"
  echo
  echo "NOTE: forked-daapd needs to be running..."
  exit 0
}

case $1 in
  -h|--help) usage;;
  -*)
    echo "Unrecognized option $1 (try -h for usage)"
    exit 1
    ;;
esac

[ -n "$1" ] && conf_path=$1

if [ ! -f "$conf_path" ]; then
  echo "Couldn't find config file '$conf_path' (try -h for usage)"
  exit 1
fi

logfile=`awk '$1=="logfile"{print $3}' $conf_path`
logfile="${logfile%\"}"
logfile="${logfile#\"}"
[ -z "$logfile" ] && logfile="/var/log/forked-daapd.log"
if [ ! -r "$logfile" ]; then
  echo "Error: Couldn't read logfile '$logfile'"
  echo "Verify 'logfile' setting in config file '$conf_path'"
  exit 1
fi

library_path=`awk '$1=="directories"{print}' $conf_path`
library_path="${library_path#*\"}"
library_path="${library_path%%\"*}"
if [ -z "$library_path" ]; then
  echo "Couldn't find 'directories' setting in config file '$conf_path'"
  exit 1
fi
if [ ! -d "$library_path" ]; then
  echo "Error: Couldn't find library '$library_path'"
  echo "Verify 'directories' setting in config file '$conf_path'"
  exit 1
fi

rf="$library_path/pair.remote"
[ -f "$rf" ] && rm -f "$rf"
[ -f "$rf" ] && echo "Unable to remove existing pairing file '$rf'" && exit 1

echo "This script will help you pair Remote with forked-daapd"
echo "Please verify that these paths are correct:"
echo "  Log file: '$logfile'"
echo "  Library:  '$library_path'"
read -p "Confirm? [Y/n] " yn
case "$yn" in
  [N]*|[n]*) exit;;
esac

echo "Please start the pairing process in Remote by selecting Add library"
read -p "Press ENTER when ready..." yn
printf %s "Looking in $logfile for Remote announcement..."

n=5
while [ $n -gt 0 ]; do
  n=`expr $n - 1`
  remote=`tail -50 "$logfile" | grep "Discovered remote" | tail -1 | grep -o "'.*' ("`
  remote="${remote%\'\ \(}"
  remote="${remote#\'}"
  [ -n "$remote" ] && break
  sleep 2
done

if [ -z "$remote" ]; then
  echo "not found!"
  exit 1
fi
echo "found"

read -p "Ready to pair Remote '$remote', please enter PIN: " pin
if [ -z "$pin" ]; then
  echo "Error: Invalid PIN"
  exit 1
fi

echo "Writing pair.remote to $library_path..."
printf "$pin" > "$rf"
if [ ! -f "$rf" ]; then
  echo "Unable to create '$rf' - check directory permissions"
  exit 1
fi

# leave enough time for deferred file processing on BSD
n=20
echo "Waiting for pairing to complete (up to $n secs)..."
while [ $n -gt 0 ]; do
  n=`expr $n - 1`
  result=`tail -1000 "$logfile" | sed -n "/.*remote:/ s,.*remote: ,,p" | awk '/^Discovered remote/{ f="" } /^Kickoff pairing with pin/ { f=$0; } END { print f }'`
  [ -n "$result" ] && break
  sleep 1
done
if [ -z "$result" ]; then
  echo "forked-daap doesn't appear to be finding $rf..."
  echo "Check $logfile, removing pair.remote"
  rm "$rf"
  exit 1
fi
echo "Pairing file pair.remote read, removing it"
rm "$rf"

n=5
while [ $n -gt 0 ]; do
  n=`expr $n - 1`
  result=`tail -1000 "$logfile" | sed -n "/.*remote:/ s,.*remote: ,,p" | awk '/^Discovered remote/{ f="" } /^Pairing succeeded/ { f=$0; } END { print f }'`
  if [ -n "$result" ]; then
    echo "All done"
    exit
  fi
  sleep 1
done
echo "Pairing appears to have failed... check $rf for details"
exit 1

