# Configuration

The configuration of OwnTone is usually located in `/etc/owntone.conf`.

## Format

Each setting consists of a name and a value. There are different types of settings: string, integer, boolean, and list.

Comments are preceded by a hash sign.

The format is as follow:

```conf
# Section
section {
    # String value
    setting = "<string-value>"
    # Integer value
    setting = <integer-value>
    # Boolean
    setting = <true|false>
    # List
    setting = { "value a", "value b", "value n"}
}
```

Some settings are device specific, in which case you add a section where you specify the device name in the heading. Say you're tired of loud death metal coming from your teenager's room:

```conf
airplay "Jared's Room" {
    max_volume = 3
}
```

## Most important settings

### general: uid

Identifier of the user running OwnTone.

Make sure that this user has read access to your configuration of `directories` in the `library` config section, and has write access to the database (`db_path`), cache directory (`cache_dir`) and log file (`logfile`). If you plan on using local audio then the user must also have access to that.

### library: directories

Path to the directory or directories containing the media to index (your library).

### library: directory aliases

By default clients browsing by folder see your library below its full path, so
a library in `/srv/music` is presented as `srv` → `music` → your music. MPD
clients in particular make you click through every one of those levels.

Giving a directory an alias replaces the path with a name of your choosing:

```conf
library {
    directories = { "/Media/Music", "/Media/Books" }

    directory "/Media/Music" { alias = "Music" }
    directory "/Media/Books" { alias = "Books" }
}
```

Clients then see `Music` and `Books` at the top level. Only the presentation
changes, so all other settings that take a path — `podcasts`, `audiobooks`,
`compilations`, `default_playlist_directory` — keep using the real path.

Directories without an alias are presented as before, which means adding this to
an existing configuration changes nothing until you actually configure an alias.

An alias must be a single name without `/`, and must belong to a directory
listed in `directories`. Two directories cannot share an alias, an alias cannot
collide with the first path component of a directory that has no alias, and
directories with an alias cannot be nested. OwnTone refuses to start otherwise.

!!! warning "Trigger a full rescan after changing an alias"
    Aliases are stored with each item when it is scanned and are not updated
    retroactively. After adding, changing or removing an alias you must trigger
    a full rescan, otherwise files scanned earlier disappear from the folder
    tree (they remain reachable by artist, album and search). OwnTone logs a
    warning at startup when it detects this.

    Note that clients may have stored references to the old paths, for example
    in their own playlists or favourites. Those break when an alias changes.

## Other settings

See the [template configuration file](https://raw.githubusercontent.com/owntone/owntone-server/refs/heads/master/owntone.conf.in) for a description of all the settings.
