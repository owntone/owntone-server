
export default class Albums {
  constructor (items, options = { hideSingles: false, hideSpotify: false, sort: 'Name', group: false }) {
    this.items = items
    this.options = options
    this.grouped = {}
    this.sortedAndFiltered = []
    this.indexList = []

    this.init()
  }

  init () {
    this.createSortedAndFilteredList()
    this.createGroupedList()
    this.createIndexList()
  }

  getAlbumIndex (album) {
    if (this.options.sort === 'Recently added') {
      return album.time_added.substring(0, 4)
    } else if (this.options.sort === 'Recently released') {
      return album.date_released ? album.date_released.substring(0, 4) : '0000'
    } else if (this.options.sort === 'Release date') {
      return album.date_released ? album.date_released.substring(0, 4) : '0000'
    }
    return album.name_sort.charAt(0).toUpperCase()
  }

  isAlbumVisible (album) {
    if (this.options.hideSingles && album.track_count <= 2) {
      return false
    }
    if (this.options.hideSpotify && album.data_kind === 'spotify') {
      return false
    }
    return true
  }

  createIndexList () {
    this.indexList = [...new Set(this.sortedAndFiltered
      .map(album => this.getAlbumIndex(album)))]
  }

  createSortedAndFilteredList () {
    var albumsSorted = this.items
    if (this.options.hideSingles || this.options.hideSpotify || this.options.hideOther) {
      albumsSorted = albumsSorted.filter(album => this.isAlbumVisible(album))
    }
    if (this.options.sort === 'Recently added') {
      albumsSorted = [...albumsSorted].sort((a, b) => b.time_added.localeCompare(a.time_added))
    } else if (this.options.sort === 'Recently released') {
      albumsSorted = [...albumsSorted].sort((a, b) => {
        if (!a.date_released) {
          return 1
        }
        if (!b.date_released) {
          return -1
        }
        return b.date_released.localeCompare(a.date_released)
      })
    } else if (this.options.sort === 'Release date') {
      albumsSorted = [...albumsSorted].sort((a, b) => {
        if (!a.date_released) {
          return -1
        }
        if (!b.date_released) {
          return 1
        }
        return a.date_released.localeCompare(b.date_released)
      })
    }
    this.sortedAndFiltered = albumsSorted
  }

  createGroupedList () {
    if (!this.options.group) {
      this.grouped = {}
    }
    this.grouped = this.sortedAndFiltered.reduce((r, album) => {
      const idx = this.getAlbumIndex(album)
      r[idx] = [...r[idx] || [], album]
      return r
    }, {})
  }
}
