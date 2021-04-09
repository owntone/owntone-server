#!/bin/sh
 
# PROVIDE: owntone
# REQUIRE: avahi_daemon dbus
 
# Add the following lines to /etc/rc.conf to enable `owntone':
#
# owntone_enable="YES"
# owntone_flags="<set as needed>"
 
. /etc/rc.subr
 
name="owntone"
rcvar=`set_rcvar`
 
command="/usr/local/sbin/owntone"
command_args="-P /var/run/owntone.pid"
pidfile="/var/run/owntone.pid"
required_files="/usr/local/etc/owntone.conf"
 
# read configuration and set defaults
load_rc_config "$name"
: ${owntone_enable="NO"}
 
run_rc_command "$1"
