#!/bin/sh
XX='	'
echo "Searching for ${XX}."

egrep -l "${XX}" *.[ch]
