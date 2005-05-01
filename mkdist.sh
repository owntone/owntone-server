#!/bin/bash

mv configure.in configure.in.mkdist
cat configure.in.mkdist | sed -e s/AM_INIT_AUTOMAKE.*$/AM_INIT_AUTOMAKE\(mt-daapd,cvs-`date +%Y%m%d`\)/ > configure.in
./reconf
./configure --with-id3tag=/opt/local
make dist
mv configure.in.mkdist configure.in


