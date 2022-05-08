# Artwork

OwnTone has support for PNG and JPEG artwork which is either:

- embedded in the media files
- placed as separate image files in the library
- made available online by the radio station

For media in your library, OwnTone will try to locate album and artist
artwork (group artwork) by the following procedure:

- if a file {artwork,cover,Folder}.{png,jpg} is found in one of the directories
  containing files that are part of the group, it is used as the artwork. The
  first file found is used, ordering is not guaranteed;
- failing that, if [directory name].{png,jpg} is found in one of the
  directories containing files that are part of the group, it is used as the
  artwork. The first file found is used, ordering is not guaranteed;
- failing that, individual files are examined and the first file found 
  with an embedded artwork is used. Here again, ordering is not guaranteed.

{artwork,cover,Folder} are the default, you can add other base names in the
configuration file. Here you can also enable/disable support for individual
file artwork (instead of using the same artwork for all tracks in an entire
album).

For playlists in your library, say /foo/bar.m3u, then for any http streams in
the list, OwnTone will look for /foo/bar.{jpg,png}.

You can use symlinks for the artwork files.

OwnTone caches artwork in a separate cache file. The default path is 
`/var/cache/owntone/cache.db` and can be configured in the configuration 
file. The cache.db file can be deleted without losing the library and pairing 
informations.
