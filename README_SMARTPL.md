# forked-daapd smart playlists


To add a smart playlist to forked-daapd, create a new text file with a filename ending with .smartpl; 
the filename doesn't matter, only the .smartpl ending does. The file must be placed somewhere in your
library folder.


## Syntax

The contents of a smart playlist must follow the syntax:

```
"Playlist Name" { expression }
```

There is exactly one smart playlist allowed for a .smartpl file.


An expression consists of:

```
field-name operator operand
```

Where valid field-names (with there types) are:
* artist (string)
* album_artist (string)
* album (string)
* title (string)
* genre (string)
* composer (string)
* path (string)
* type (string)
* data_kind (enumeration)
* media_kind (enumeration)
* play_count (integer)
* rating (integer)
* year (integer)
* compilation (integer)
* time_added (date)
* time_played (date)

Valid operators include:
* is, includes (string)
* >, <, <=, >=, = (int)
* after, before (date)
* is (enumeration)

The "is" operator must exactly match the field value, while the "includes" operator matches a substring.
Both matches are case-insensitive.

Valid operands include:
* "string value" (string)
* integer (int)

Valid operands for the enumeration "data_kind" are:
* file
* url
* spotify
* pipe

Valid operands for the enumeration "media_kind" are:
* music
* movie
* podcast
* audiobook
* tvshow


Multiple expressions can be anded or ored together, using the keywords OR and AND. The unary not operator is also supported using the keyword NOT.

Examples:

```
"techno" {
   genre includes "techno"
   and artist includes "zombie"
}
```

This would match songs by "Rob Zombie" or "White Zombie", as well as those with a genre of "Techno-Industrial" or
"Trance/Techno", for example.

```
"techno 2015" {
   genre includes "techno"
   and artist includes "zombie"
   and not genre includes "industrial"
}
```

This would exclude e. g. songs with the genre "Techno-Industrial".

```
"Local music" {
  data_kind is file
  and media_kind is music
}
```

This would match all songs added as files to the library that are not placed under the folders for podcasts, audiobooks.

```
"Unplayed podcasts and audiobooks" {
  play_count = 0
  and (media_kind is podcast or media_kind is audiobook)
}
```

This would match any podcast and audiobook file that was never played with forked-daapd.


## Date operand syntax

One example of a valid date is a date in yyyy-mm-dd format:

```
"Files added after January 1, 2004" {
  time_added after 2004-01-01
}
```

There are also some special date keywords:
* "today", "yesterday", "last week", "last month", "last year"

A valid date can also be made by appling an interval to a date. Intervals can be defined as "days", "weeks", "months", "years".
As an example, a valid date might be:

```3 weeks before today``` or ```3 weeks ago```


Examples:

```
"Recently Added" {
    time_added after 2 weeks ago
}
```

This matches all songs added in the last 2 weeks.

```
"Recently played audiobooks" {
    time_played after last week
    and media_kind is audiobook
}
```

This matches all audiobooks played in the last week.


## Differences to mt-daapd smart playlists

The syntax is really close to the mt-daapd smart playlist syntax (see
http://sourceforge.net/p/mt-daapd/code/HEAD/tree/tags/release-0.2.4.2/contrib/mt-daapd.playlist).

Even this documentation is based on the file linked above.

Some differences are:
* only one smart playlist per file
* the not operator must be placed before an expression and not before the operator
* "||", "&&", "!" are not supported (use "or", "and", "not")
* comments are not supported

