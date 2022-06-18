#!/bin/sh

set -x

DEFAULT_AUTORECONF_FLAGS="-i"
DEFAULT_CONFIGURE_FLAGS="--prefix=/usr --sysconfdir=/etc --localstatedir=/var"
DEFAULT_MAKE_FLAGS=""

autoreconf ${AUTORECONF_FLAGS:-${DEFAULT_AUTORECONF_FLAGS}}
./configure ${CONFIGURE_FLAGS:-${DEFAULT_CONFIGURE_FLAGS}}
make web
make ${MAKE_FLAGS:-${DEFAULT_MAKE_FLAGS}}
