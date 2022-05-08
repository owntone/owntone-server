# Running Multiple Instances

To run multiple instances of owntone on a server, you should copy
`/etc/owntone.conf` to `/etc/owntone-zone.conf` (for each `zone`) and
modify the following to be unique across all instances:

* the three port settings (`general` -> `websocket_port`,
  `library` -> `port`, and `mpd` -> `port`)

* the database paths (`general` -> `db_path`, `db_backup_path`, and `db_cache_path`)

* the service name (`library` -> `name`).

* you probably also want to disable local output (set `audio` -> `type =
  "disabled"`).

Then run `owntone -c /etc/owntone-zone.conf` to run owntone with the new
zone configuration.

Owntone has a `systemd` template which lets you run this automatically
on systems that use systemd.  You can start or enable the service for
a `zone` by `sudo systemctl start owntone@zone` and check that it is
running with `sudo systemctl status owntone@zone`.  Use `sudo
systemctl enable ownton@zone` to get the service to start on reboot.

