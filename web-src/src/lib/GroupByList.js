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
      } else {
        return '⌘'
      }
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

export class GroupByList {
  constructor({ items = [], total = 0, offset = 0, limit = -1 } = {}) {
    this.items = items
    this.total = total
    this.offset = offset
    this.limit = limit

    this.count = items.length
    this.indexList = []
    this.group(noop())
  }

  get() {
    return this.itemsByGroup
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
    let itemsSorted = options.compareFn
      ? [...itemsFiltered].sort(options.compareFn)
      : itemsFiltered

    // Create index list
    this.indexList = [...new Set(itemsSorted.map(options.groupKeyFn))]

    // Group item list
    this.itemsByGroup = itemsSorted.reduce((r, item) => {
      const groupKey = options.groupKeyFn(item)
      r[groupKey] = [...(r[groupKey] || []), item]
      return r
    }, {})
  }

  [Symbol.iterator]() {
    // Use a new index for each iterator. This makes multiple
    // iterations over the iterable safe for non-trivial cases,
    // such as use of break or nested looping over the same iterable.
    let groupIndex = -1
    let itemIndex = -1

    return {
      next: () => {
        if (this.isEmpty()) {
          return { done: true }
        } else if (groupIndex >= this.indexList.length) {
          // We reached the end of all groups and items
          //
          // This should never happen, as the we already
          // return "done" after we reached the last item
          // of the last group
          return { done: true }
        } else if (groupIndex < 0) {
          // We start iterating
          //
          // Return the first group title as the next item
          ++groupIndex
          itemIndex = 0

          if (this.indexList[groupIndex] !== GROUP_KEY_NONE) {
            // Only return the group, if it is not the "noop" default group
            return {
              value: {
                groupKey: this.indexList[groupIndex],
                itemId: this.indexList[groupIndex],
                isItem: false,
                item: {}
              },
              done: false
            }
          }
        }

        let currentGroupKey = this.indexList[groupIndex]
        let currentGroupItems = this.itemsByGroup[currentGroupKey]

        if (itemIndex < currentGroupItems.length) {
          // We are in a group with items left
          //
          // Return the current item and increment the item index
          const currentItem = this.itemsByGroup[currentGroupKey][itemIndex++]
          return {
            value: {
              groupKey: currentGroupKey,
              itemId: currentItem.id,
              isItem: true,
              item: currentItem
            },
            done: false
          }
        } else {
          // We reached the end of the current groups item list
          //
          // Move to the next group and return the group key/title
          // as the next item
          ++groupIndex
          itemIndex = 0

          if (groupIndex < this.indexList.length) {
            currentGroupKey = this.indexList[groupIndex]
            return {
              value: {
                groupKey: currentGroupKey,
                itemId: currentGroupKey,
                isItem: false,
                item: {}
              },
              done: false
            }
          } else {
            // No group left, we are done iterating
            return { done: true }
          }
        }
      }
    }
  }
}
