import i18n from '@/i18n'

const { locale } = i18n.global
const DATE_BASE = { day: 'numeric', month: 'long', year: 'numeric' }
const TIME_BASE = { hour: '2-digit', minute: '2-digit' }
const FORMATS = { date: DATE_BASE, dateTime: { ...DATE_BASE, ...TIME_BASE } }
const UNITS = { hour: 3_600_000, minute: 60_000, second: 1_000 }
const BASES = [
  ['minute', (date) => date.getMinutes(), 60],
  ['hour', (date) => date.getHours(), 24],
  ['day', (date) => date.getDate(), null],
  ['month', (date) => date.getMonth(), 12],
  ['year', (date) => date.getFullYear(), null]
]

const pad = (num) => String(num).padStart(2, '0')
const getRTF = () => new Intl.RelativeTimeFormat(locale.value)
const getDTF = (format) => new Intl.DateTimeFormat(locale.value, format)
const getNTF = (unit) =>
  new Intl.NumberFormat(locale.value, {
    style: 'unit',
    unit,
    unitDisplay: 'long'
  })

const formatParts = (parts) =>
  parts.map(({ value, unit }) => getNTF(unit).format(value)).join(', ')

const applyCarries = (result, start) => {
  let index = 0
  while (index < BASES.length - 1) {
    const [key, , base] = BASES[index]
    if (result[key] < 0) {
      const [parentKey] = BASES[index + 1]
      result[parentKey] -= 1
      result[key] +=
        base ?? new Date(start.getFullYear(), start.getMonth() + 1).getDate()
      index = 0
    } else {
      index += 1
    }
  }
  return result
}

const difference = (date1, date2) => {
  const start = new Date(Math.min(date1, date2))
  const end = new Date(Math.max(date1, date2))
  const deltas = Object.fromEntries(
    BASES.map(([key, getter]) => [key, getter(end) - getter(start)])
  )
  return applyCarries(deltas, start)
}

const toParts = (milliseconds) => {
  const diff = difference(Date.now(), Date.now() + milliseconds)
  return BASES.filter(([unit]) => diff[unit] > 0 || unit === 'minute')
    .map(([unit]) => ({ unit, value: diff[unit] }))
    .reverse()
}

export default {
  toDate(date = 0) {
    return getDTF(FORMATS.date).format(new Date(date))
  },
  toDateTime(date = 0) {
    return getDTF(FORMATS.dateTime).format(new Date(date))
  },
  toDuration(milliseconds = 0) {
    return formatParts(toParts(milliseconds))
  },
  toDurationSince(date = 0) {
    return formatParts(toParts(Date.now() - new Date(date).getTime()))
  },
  toRelativeDuration(date = 0) {
    const [{ value, unit }] = toParts(Date.now() - new Date(date).getTime())
    return getRTF().format(-value, unit)
  },
  toTimecode(milliseconds = 0) {
    const hours = Math.floor(milliseconds / UNITS.hour)
    const minutes = Math.floor((milliseconds % UNITS.hour) / UNITS.minute)
    const seconds = Math.floor((milliseconds % UNITS.minute) / UNITS.second)
    return [hours, pad(minutes), pad(seconds)].join(':').replace(/^0:/u, '')
  }
}
