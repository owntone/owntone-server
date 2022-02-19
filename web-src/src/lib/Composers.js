export default class Composers {
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

  getComposerIndex(composer) {
    if (this.options.sort === 'Name') {
      return composer.name_sort.charAt(0).toUpperCase()
    }
    return composer.time_added.substring(0, 4)
  }

  isComposerVisible(composer) {
    if (
      this.options.hideSingles &&
      composer.track_count <= composer.album_count * 2
    ) {
      return false
    }
    if (this.options.hideSpotify && composer.data_kind === 'spotify') {
      return false
    }
    return true
  }

  createIndexList() {
    this.indexList = [
      ...new Set(
        this.sortedAndFiltered.map((composer) =>
          this.getComposerIndex(composer)
        )
      )
    ]
  }

  createSortedAndFilteredList() {
    let composersSorted = this.items
    if (
      this.options.hideSingles ||
      this.options.hideSpotify ||
      this.options.hideOther
    ) {
      composersSorted = composersSorted.filter((composer) =>
        this.isComposerVisible(composer)
      )
    }
    if (this.options.sort === 'Recently added') {
      composersSorted = [...composersSorted].sort((a, b) =>
        b.time_added.localeCompare(a.time_added)
      )
    }
    this.sortedAndFiltered = composersSorted
  }

  createGroupedList() {
    if (!this.options.group) {
      this.grouped = {}
    }
    this.grouped = this.sortedAndFiltered.reduce((r, composer) => {
      const idx = this.getComposerIndex(composer)
      r[idx] = [...(r[idx] || []), composer]
      return r
    }, {})
  }
}
