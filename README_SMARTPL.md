# OwnTone smart playlists


To add a smart playlist to the server, create a new text file with a filename ending with .smartpl; 
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

Where valid field-names (with their types) are:
* `artist` (string)
* `album_artist` (string)
* `album` (string)
* `title` (string)
* `genre` (string)
* `composer` (string)
* `comment` (string)
* `path` (string)
* `type` (string)
* `grouping` (string)
* `data_kind` (enumeration)
* `media_kind` (enumeration)
* `play_count` (integer)
* `skip_count` (integer)
* `rating` (integer)
* `year` (integer)
* `compilation` (integer)
* `track` (integer)
* `disc` (integer)
* `time_added` (date)
* `time_modified` (date)
* `time_played` (date)
* `time_skipped` (date)
* `random` (special)

Valid operators include:
* `is`, `includes`, `starts with`, `ends with` (string)
* `>`, `<`, `<=`, `>=`, `=` (int)
* `after`, `before` (date)
* `is` (enumeration)

The `is` operator must exactly match the field value, while the `includes` operator matches a substring.
The `starts with` operator matches, if the value starts with the given prefix,
and `ends with` matches the opposite. All these matches are case-insensitive.

Valid operands include:
* "string value" (string)
* integer (int)

Valid operands for the enumeration `data_kind` are:
* `file`
* `url`
* `spotify`
* `pipe`

Valid operands for the enumeration `media_kind` are:
* `music`
* `movie`
* `podcast`
* `audiobook`
* `tvshow`


Multiple expressions can be anded or ored together, using the keywords `OR` and `AND`. The unary not operator is also supported using the keyword `NOT`.


It is possible to define the sort order and limit the number of items by adding an order clause and/or a limit clause after the last expression:

```
"Playlist Name" { expression ORDER BY field-name sort-direction LIMIT limit }
```

"sort-direction" is either `ASC` (ascending) or `DESC` (descending). "limit" is the maximum number of items.

There is additionally a special `random` _field-name_ that can be used in conjunction with `limit` to select a random number of items based on current expression.


## Examples

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

This would match any podcast and audiobook file that was never played.

```
"Recently added music" {
  media_kind is music
  order by time_added desc
  limit 10
}
```
This would match the last 10 music files added to the library.

```
"Random 10 Rated Pop songs" {
  rating > 0 and
  genre is "Pop" and
  media_kind is music
  order by random desc
  limit 10
}
```
This generates a random set of, maximum of 10, rated Pop music tracks every time the playlist is queried.

## Date operand syntax

One example of a valid date is a date in yyyy-mm-dd format:

```
"Files added after January 1, 2004" {
  time_added after 2004-01-01
}
```

There are also some special date keywords:
* `today`, `yesterday`, `this week`, `last week`, `last month`, `last year`

These dates refer to the _start_ of that period; `today` means 00:00hrs of today, `this week` means current Monday 00:00hrs, `last week` means the previous Monday 00:00hrs, `last month` is the first day of the previous month at 00:00hrs etc.

A valid date can also be made by applying an interval to a date. Intervals can be defined as `days`, `weeks`, `months`, `years`.
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

This matches all audiobooks played since the start of the last Monday 00:00AM.

All dates, except for `YYYY-DD-HH`, are relative to the day of when the server evaluates the smartpl query; `time_added after today` run on a Monday would match against items added since Monday 00:00hrs and evaluating the same smartpl on Friday would only match against added on Friday 00:00hrs.

Note that `time_added after 4 weeks ago` and `time_added after last month` are subtly different; the former is exactly 4 weeks ago (from today) whereas the latter is the first day of the previous month.


## Differences to mt-daapd smart playlists

The syntax is really close to the mt-daapd smart playlist syntax (see
http://sourceforge.net/p/mt-daapd/code/HEAD/tree/tags/release-0.2.4.2/contrib/mt-daapd.playlist).

Even this documentation is based on the file linked above.

Some differences are:
* only one smart playlist per file
* the not operator must be placed before an expression and not before the operator
* "||", "&&", "!" are not supported (use "or", "and", "not")
* comments are not supported

