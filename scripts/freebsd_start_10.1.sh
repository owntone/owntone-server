#!/bin/sh
 
# PROVIDE: forked-daapd
# REQUIRE: avahi_daemon dbus
 
# Add the following lines to /etc/rc.conf to enable `forked-daapd':
#
# forked_daapd_enable="YES"
# forked_daapd_flags="<set as needed>"
 
. /etc/rc.subr
 
name="forked_daapd"
rcvar=`set_rcvar`
 
command="/usr/local/sbin/forked-daapd"
command_args="-P /var/run/forked-daapd.pid"
pidfile="/var/run/forked-daapd.pid"
required_files="/usr/local/etc/forked-daapd.conf"
 
# read configuration and set defaults
load_rc_config "$name"
: ${forked_daapd_enable="NO"}
 
run_rc_command "$1"
