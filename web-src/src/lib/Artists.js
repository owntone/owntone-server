export default class Artists {
  constructor(
    items,
    options = {
      hideSingles: false,
      hideSpotify: false,
      sort: 'Name',
      group: false
    }
  ) {
    this.items = items
    this.options = options
    this.grouped = {}
    this.sortedAndFiltered = []
    this.indexList = []

    this.init()
  }

  init() {
    this.createSortedAndFilteredList()
    this.createGroupedList()
    this.createIndexList()
  }

  getArtistIndex(artist) {
    if (this.options.sort === 'Name') {
      return artist.name_sort.charAt(0).toUpperCase()
    }
    return artist.time_added.substring(0, 4)
  }

  isArtistVisible(artist) {
    if (
      this.options.hideSingles &&
      artist.track_count <= artist.album_count * 2
    ) {
      return false
    }
    if (this.options.hideSpotify && artist.data_kind === 'spotify') {
      return false
    }
    return true
  }

  createIndexList() {
    this.indexList = [
      ...new Set(
        this.sortedAndFiltered.map((artist) => this.getArtistIndex(artist))
      )
    ]
  }

  createSortedAndFilteredList() {
    let artistsSorted = this.items
    if (
      this.options.hideSingles ||
      this.options.hideSpotify ||
      this.options.hideOther
    ) {
      artistsSorted = artistsSorted.filter((artist) =>
        this.isArtistVisible(artist)
      )
    }
    if (this.options.sort === 'Recently added') {
      artistsSorted = [...artistsSorted].sort((a, b) =>
        b.time_added.localeCompare(a.time_added)
      )
    }
    this.sortedAndFiltered = artistsSorted
  }

  createGroupedList() {
    if (!this.options.group) {
      this.grouped = {}
    }
    this.grouped = this.sortedAndFiltered.reduce((r, artist) => {
      const idx = this.getArtistIndex(artist)
      r[idx] = [...(r[idx] || []), artist]
      return r
    }, {})
  }
}
