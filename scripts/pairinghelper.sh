#!/bin/sh

# Set location of the config file
conf_path=/etc/forked-daapd.conf

if [ ! -f $conf_path ]; then
	echo "Error: Couldn't find $conf_path"
	echo "Set the correct config file location in the script"
	exit
fi

logfile=`awk '$1=="logfile"{print $3}' $conf_path`
logfile="${logfile%\"}"
logfile="${logfile#\"}"
library_path=`awk '$1=="directories"{print}' $conf_path`
library_path="${library_path#*\"}"
library_path="${library_path%%\"*}"

if [ ! -f $logfile ]; then
	echo "Error: Couldn't find logfile in $logfile"
	exit
fi
if [ ! -d $library_path ]; then
	echo "Error: Couldn't find library in $library_path"
	exit
fi

echo "This script will help you pair Remote with forked-daapd"
echo "Please verify that these paths are correct:"
echo "  Log file: $logfile"
echo "  Library:  $library_path"
read -p "Confirm? [Y/n] " yn
if [ "$yn" = "n" ]; then
	exit
fi
echo "Please start the pairing process in Remote by selecting Add library"
read -p "Press ENTER when ready..." yn
echo -n "Looking in $logfile for Remote announcement..."
sleep 5

remote=`grep "Discovered remote" $logfile | tail -1 | grep -Po "'.*?'"`
remote="${remote%\'}"
remote="${remote#\'}"

if [ -z "$remote" ]; then
	echo "not found"
	exit
else
	echo "found"
fi

read -p "Ready to pair Remote '$remote', please enter PIN: " pin
if [ -z "$pin" ]; then
	echo "Error: Invalid PIN"
	exit
fi

echo "Writing pair.remote to $library_path..."
printf "$remote\n$pin" > "$library_path/pair.remote"
sleep 1
echo "Removing pair.remote from library again..."
rm "$library_path/pair.remote"
echo "All done"

