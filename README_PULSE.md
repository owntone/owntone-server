# forked-daapd and Pulseaudio

## Setting up Pulseaudio

If you see a "Connection refused" error when starting forked-daapd, then you
will probably need to setup Pulseaudio to run in system mode. This means that
the Pulseaudio daemon will be started during boot and be available to all users.

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

After you have added the file you can start it with "systemctl start pulseaudio"
and enable it on boot with "systemctl enable pulseaudio".


## Setting up forked-daapd:

Add the forked-daapd user to the pulse-access group:

```
adduser daapd pulse-access
```


## Bluetooth

[TBD]

(this page is work in progress)

