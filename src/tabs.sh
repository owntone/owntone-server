#!/bin/sh
XX='	'
echo "Searching for Tabs..."

egrep -r -l "${XX}" * | grep '[ch]$'

echo "Searching for windows line endings..."

grep -r -l $'\r' * | grep '[ch]$'