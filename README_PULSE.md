# forked-daapd and Pulseaudio
Credit: [Rob Pope](http://robpope.co.uk/blog/post/setting-up-forked-daapd-with-bluetooth)

This guide was written based on headless Debian Jessie platforms. Most of the
instructions will require that you are root.


## Step 1: Setting up Pulseaudio in system mode with Bluetooth support

If you see a "Connection refused" error when starting forked-daapd, then you
will probably need to setup Pulseaudio to run in system mode [1]. This means
that the Pulseaudio daemon will be started during boot and be available to all
users.

How to start Pulseaudio depends on your distribution, but in many cases you will
need to add a pulseaudio.service file to /etc/systemd/system with the following
content:

```
# systemd service file for Pulseaudio running in system mode
[Unit]
Description=Pulseaudio sound server
Before=sound.target

[Service]
ExecStart=/usr/bin/pulseaudio --system --disallow-exit

[Install]
WantedBy=multi-user.target
```

If you want Bluetooth support, you must also configure Pulseaudio to load the
Bluetooth module. First install it (Debian: 
`apt install pulseaudio-module-bluetooth`) and then add the following to
/etc/pulse/system.pa:

```
### Enable Bluetooth
.ifexists module-bluetooth-discover.so
load-module module-bluetooth-discover
.endif
```

Now you need to make sure that Pulseaudio can communicate with the Bluetooth
daemon through D-Bus. On Raspbian this is already enabled, and you can skip this
step. Otherwise do one of the following:

1. Add the pulse user to the bluetooth group: `adduser pulse bluetooth`
2. Edit /etc/dbus-1/system.d/bluetooth.conf and change the policy for
\<policy context="default"\> to "allow"

Phew, almost done with Pulseaudio! Now you should:
1. enable system mode on boot with `systemctl enable pulseaudio`
2. reboot (or at least restart dbus and pulseaudio)
3. check that the Bluetooth module is loaded with `pactl list modules short`


## Step 2: Setting up forked-daapd

Add the user forked-daapd is running as (typically "daapd") to the
"pulse-access" group:

```
adduser daapd pulse-access
```

Now (re)start forked-daapd.


## Step 3: Adding a Bluetooth device

To connect with the device, run `bluetoothctl` and then:

```
power on
agent on
scan on
**Note MAC address of BT Speaker**
pair [MAC address]
**Type Pin if prompted**
trust [MAC address]
connect [MAC address]
```

Now the speaker should appear in forked-daapd. You can also verify that 
Pulseaudio has detected the speaker with `pactl list sinks short`.

---

[1] Note that Pulseaudio will warn against system mode. However, in this use
case it is actually the solution recommended by the [Pulseaudio folks themselves](https://lists.freedesktop.org/archives/pulseaudio-discuss/2016-August/026823.html).

