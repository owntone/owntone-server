# Remote access

It is possible to access a shared library over the internet from a DAAP client
like iTunes. You must have remote access to the host machine.

First log in to the host and forward port 3689 to your local machine. You now
need to broadcast the daap service to iTunes on your local machine. On macOS the
command is:

```
dns-sd -P iTunesServer _daap._tcp local 3689 localhost.local 127.0.0.1 "ffid=12345"
```

The `ffid` key is required but its value does not matter.

Your library will now appear as 'iTunesServer' in iTunes.

You can also access your library remotely using something like Zerotier. See [this
guide](https://github.com/owntone/owntone-server/wiki/Accessing-Owntone-remotely-through-iTunes-Music-with-Zerotier)
for details.

