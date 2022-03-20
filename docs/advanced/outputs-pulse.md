# OwnTone and Pulseaudio

You have the choice of runnning Pulseaudio either in system mode or user mode.
For headless servers, i.e. systems without desktop users, system mode is
recommended.

If there is a desktop user logged in most of the time, a setup with network
access via localhost only for daemons is a more appropriate solution, since the
normal user administration (with, e.g., `pulseaudio -k`) works as advertised.
Also, the user specific configuration for pulseaudio is preserved across
sessions as expected.

- [System mode](#system-mode-with-bluetooth-support)
- [User mode](#user-mode-with-network-access)


## System Mode with Bluetooth support

Credit: [Rob Pope](http://robpope.co.uk/blog/post/setting-up-forked-daapd-with-bluetooth)

This guide was written based on headless Debian Jessie platforms. Most of the
instructions will require that you are root.


### Step 1: Setting up Pulseaudio

If you see a "Connection refused" error when starting the server, then you
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
#### Enable Bluetooth
.ifexists module-bluetooth-discover.so
load-module module-bluetooth-discover
.endif
```

Now you need to make sure that Pulseaudio can communicate with the Bluetooth
daemon through D-Bus. On Raspbian this is already enabled, and you can skip this
step. Otherwise do one of the following:

1. Add the pulse user to the bluetooth group: `adduser pulse bluetooth`
2. Edit /etc/dbus-1/system.d/bluetooth.conf and change the policy for `<policy context="default"\>` to "allow"

Phew, almost done with Pulseaudio! Now you should:

1. enable system mode on boot with `systemctl enable pulseaudio`
2. reboot (or at least restart dbus and pulseaudio)
3. check that the Bluetooth module is loaded with `pactl list modules short`


### Step 2: Setting up the server

Add the user the server is running as (typically "owntone") to the
"pulse-access" group:

```
adduser owntone pulse-access
```

Now (re)start the server.


### Step 3: Adding a Bluetooth device

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

Now the speaker should appear. You can also verify that Pulseaudio has detected
the speaker with `pactl list sinks short`.



## User Mode with Network Access

Credit: wolfmanx and [this blog](http://billauer.co.il/blog/2014/01/pa-multiple-users/)


### Step 1: Copy system pulseaudio configuration to the users home directory

```
mkdir -p ~/.pulse
cp /etc/pulse/default.pa ~/.pulse/
```


### Step 2: Enable TCP access from localhost only

Edit the file `~/.pulse/default.pa` , adding the following line at the end:

```
load-module module-native-protocol-tcp auth-ip-acl=127.0.0.1
```


### Step 3: Restart the pulseaudio deamon

```
pulseaudio -k
# OR
pulseaudio -D
```


### Step 4: Adjust configuration file

In the `audio` section of `/etc/owntone.conf`, set `server` to `localhost`:

```
server = "localhost"
```

---

[1] Note that Pulseaudio will warn against system mode. However, in this use
case it is actually the solution recommended by the [Pulseaudio folks themselves](https://lists.freedesktop.org/archives/pulseaudio-discuss/2016-August/026823.html).
