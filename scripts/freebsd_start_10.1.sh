#!/bin/sh
 
# PROVIDE: owntone-server
# REQUIRE: avahi_daemon dbus
 
# Add the following lines to /etc/rc.conf to enable `owntone-server':
#
# owntone_server_enable="YES"
# owntone_server_flags="<set as needed>"
 
. /etc/rc.subr
 
name="owntone_server"
rcvar=`set_rcvar`
 
command="/usr/local/sbin/owntone-server"
command_args="-P /var/run/owntone-server.pid"
pidfile="/var/run/owntone-server.pid"
required_files="/usr/local/etc/owntone-server.conf"
 
# read configuration and set defaults
load_rc_config "$name"
: ${owntone_server_enable="NO"}
 
run_rc_command "$1"
