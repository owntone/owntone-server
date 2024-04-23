import i18n from '@/i18n'

const { t, locale } = i18n.global
const NO_INDEX = 'NO_INDEX'

const numberComparator = (a, b) => a - b
const stringComparator = (a, b) => a.localeCompare(b, locale.value)
const dateComparator = (a, b) =>
  new Date(a) - new Date(b) || (!a ? -1 : !b ? 1 : 0)

const createComparators = (criteria) =>
  criteria.map(({ field, type, order = 1 }) => {
    switch (type) {
      case String:
        return (a, b) => stringComparator(a[field], b[field]) * order
      case Number:
        return (a, b) => numberComparator(a[field], b[field]) * order
      case Date:
        return (a, b) => dateComparator(a[field], b[field]) * order
      default:
        return (a, b) => 0
    }
  })

const characterIndex = (string = '') => {
  const value = string.charAt(0)
  if (value.match(/\p{Letter}/gu)) {
    return value.toUpperCase()
  } else if (value.match(/\p{Number}/gu)) {
    return '#'
  }
  return '⌘'
}

const numberIndex = (number) => {
  Math.floor(number / 10)
}

const times = [
  { difference: NaN, text: () => t('grouped-list.undefined') },
  { difference: 86400000, text: () => t('grouped-list.today') },
  { difference: 604800000, text: () => t('grouped-list.last-week') },
  { difference: 2592000000, text: () => t('grouped-list.last-month') },
  { difference: Infinity, text: (date) => date.getFullYear() }
]

const timeIndex = (string) => {
  const date = new Date(string)
  const diff = new Date() - date
  return times.find((item) => isNaN(diff) || diff < item.difference)?.text(date)
}

const createIndexer = ({ field, type } = {}) => {
  switch (type) {
    case String:
      return (item) => characterIndex(item[field])
    case Number:
      return (item) => item[field]
    case Date:
      return (item) => timeIndex(item[field])
    case 'Digits':
      return (item) => numberIndex(item[field])
    default:
      return (item) => NO_INDEX
  }
}

export class GroupedList {
  constructor(
    { items = [], total = 0, offset = 0, limit = -1 } = {},
    options = {}
  ) {
    this.items = items
    this.total = total
    this.offset = offset
    this.limit = limit
    this.count = items.length
    this.indices = []
    this.group(options)
  }

  group({ criteria = [], filters = [], index } = {}) {
    const indexer = createIndexer(index)
    const itemsFiltered = this.items.filter((item) =>
      filters.every((filter) => filter(item))
    )
    this.count = itemsFiltered.length
    // Sort item list
    const comparators = createComparators(criteria)
    const itemsSorted = itemsFiltered.sort((a, b) =>
      comparators.reduce(
        (comparison, comparator) => comparison || comparator(a, b),
        0
      )
    )
    // Group item list
    this.itemsGrouped = itemsSorted.reduce((map, item) => {
      const index = indexer(item)
      map.set(index, [...(map.get(index) || []), item])
      return map
    }, new Map())
    // Create index list
    this.indices = Array.from(this.itemsGrouped.keys())
  }

  *generate() {
    for (const [index, items] of this.itemsGrouped.entries()) {
      if (index !== NO_INDEX) {
        yield {
          index,
          isItem: false,
          item: {},
          itemId: index
        }
      }
      for (const item of items) {
        yield {
          index,
          isItem: true,
          item,
          itemId: item.id
        }
      }
    }
  }

  [Symbol.iterator]() {
    return this.generate()
  }
}
