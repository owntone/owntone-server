import i18n from '@/i18n'

const { t, locale } = i18n.global
const GROUP_KEY_NONE = 'GROUP_KEY_NONE'

export function noop() {
  return {
    compareFn: null,
    groupKeyFn: (item) => GROUP_KEY_NONE
  }
}

export function byName(field, keepSortOrder = false, defaultValue = '_') {
  return {
    compareFn: keepSortOrder
      ? null
      : (a, b) => {
          const fieldA = a[field] || defaultValue
          const fieldB = b[field] || defaultValue
          return fieldA.localeCompare(fieldB, locale.value)
        },

    groupKeyFn: (item) => {
      const value = (item[field] || defaultValue).charAt(0)
      if (value.match(/\p{Letter}/gu)) {
        return value.toUpperCase()
      } else if (value.match(/\p{Number}/gu)) {
        return '#'
      }
      return '⌘'
    }
  }
}

export function byRating(field, { direction = 'asc', defaultValue = 0 }) {
  return {
    compareFn: (a, b) => {
      const fieldA = a[field] || defaultValue
      const fieldB = b[field] || defaultValue
      const result = fieldA - fieldB
      return direction === 'asc' ? result : result * -1
    },

    groupKeyFn: (item) => {
      const fieldValue = item[field] || defaultValue
      return Math.floor(fieldValue / 10)
    }
  }
}

export function byMedium(field, direction = 'asc', defaultValue = 1) {
  return {
    compareFn: (a, b) => {
      const fieldA = a[field] || defaultValue
      const fieldB = b[field] || defaultValue
      const result = fieldA - fieldB
      return direction === 'asc' ? result : result * -1
    },

    groupKeyFn: (item) => {
      return item[field] || defaultValue
    }
  }
}

export function byYear(field, { direction = 'asc', defaultValue = '0000' }) {
  return {
    compareFn: (a, b) => {
      const fieldA = a[field] || defaultValue
      const fieldB = b[field] || defaultValue
      const result = fieldA.localeCompare(fieldB, locale.value)
      return direction === 'asc' ? result : result * -1
    },

    groupKeyFn: (item) => {
      const fieldValue = item[field] || defaultValue
      return fieldValue.substring(0, 4)
    }
  }
}

export function byDateSinceToday(field, defaultValue = '0000') {
  return {
    compareFn: (a, b) => {
      const fieldA = a[field] || defaultValue
      const fieldB = b[field] || defaultValue
      return fieldB.localeCompare(fieldA, locale.value)
    },

    groupKeyFn: (item) => {
      const fieldValue = item[field]
      if (!fieldValue) {
        return defaultValue
      }
      const diff = new Date().getTime() - new Date(fieldValue).getTime()
      if (diff < 86400000) {
        // 24h
        return t('group-by-list.today')
      } else if (diff < 604800000) {
        // 7 days
        return t('group-by-list.last-week')
      } else if (diff < 2592000000) {
        // 30 days
        return t('group-by-list.last-month')
      }
      return fieldValue.substring(0, 4)
    }
  }
}

export class GroupedList {
  constructor({ items = [], total = 0, offset = 0, limit = -1 } = {}) {
    this.items = items
    this.total = total
    this.offset = offset
    this.limit = limit
    this.count = items.length
    this.indices = []
    this.group(noop())
  }

  isEmpty() {
    return !this.items || this.items.length <= 0
  }

  group(options, filterFns = []) {
    const itemsFiltered = filterFns
      ? this.items.filter((item) => filterFns.every((fn) => fn(item)))
      : this.items
    this.count = itemsFiltered.length

    // Sort item list
    const itemsSorted = options.compareFn
      ? [...itemsFiltered].sort(options.compareFn)
      : itemsFiltered

    // Create index list
    this.indices = [...new Set(itemsSorted.map(options.groupKeyFn))]

    // Group item list
    this.itemsGrouped = itemsSorted.reduce((r, item) => {
      const groupKey = options.groupKeyFn(item)
      r[groupKey] = [...(r[groupKey] || []), item]
      return r
    }, {})
  }

  *generate() {
    for (const key in this.itemsGrouped) {
      if (key !== GROUP_KEY_NONE) {
        yield {
          groupKey: key,
          itemId: key,
          isItem: false,
          item: {}
        }
      }
      for (const item of this.itemsGrouped[key]) {
        yield {
          groupKey: key,
          itemId: item.id,
          isItem: true,
          item: item
        }
      }
    }
  }

  [Symbol.iterator]() {
    return this.generate()
  }
}
