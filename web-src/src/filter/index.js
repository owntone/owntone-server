import { DateTime, Duration } from 'luxon'
import i18n from '@/i18n'

const { t, locale } = i18n.global

export const filters = {
  durationInHours(value_ms) {
    const seconds = Math.floor(value_ms / 1000)
    if (seconds > 3600) {
      return Duration.fromObject({ seconds: seconds })
        .shiftTo('hours', 'minutes', 'seconds')
        .toFormat('hh:mm:ss')
    }
    return Duration.fromObject({ seconds: seconds })
      .shiftTo('minutes', 'seconds')
      .toFormat('mm:ss')
  },

  durationInDays(value_ms) {
    const minutes = Math.floor(value_ms / 60000)
    if (minutes > 1440) {
      // 60 * 24
      return Duration.fromObject({ minutes: minutes })
        .shiftTo('days', 'hours', 'minutes')
        .toHuman()
    } else if (minutes > 60) {
      return Duration.fromObject({ minutes: minutes })
        .shiftTo('hours', 'minutes')
        .toHuman()
    }
    return Duration.fromObject({ minutes: minutes })
      .shiftTo('minutes')
      .toHuman()
  },

  date(value) {
    return DateTime.fromISO(value)
      .setLocale(locale.value)
      .toLocaleString(DateTime.DATE_FULL)
  },

  datetime(value) {
    return DateTime.fromISO(value)
      .setLocale(locale.value)
      .toLocaleString(DateTime.DATETIME_MED)
  },

  timeFromNow(value) {
    const diff = DateTime.now().diff(DateTime.fromISO(value))

    return this.durationInDays(diff.as('milliseconds'))
  },

  number(value) {
    return value.toLocaleString(locale.value)
  },

  channels(value) {
    if (value === 1) {
      return t('filter.mono')
    }
    if (value === 2) {
      return t('filter.stereo')
    }
    if (!value) {
      return ''
    }
    return t('filter.channels', { value })
  },

  cursor(path, size = 20) {
    const viewbox = 24
    const center = size / 2
    return `url("data:image/svg+xml,%3Csvg width='${size}' height='${size}' viewBox='0 0 ${viewbox} ${viewbox}' xmlns='http://www.w3.org/2000/svg' %3E%3Cpath d='${path}'/%3E%3C/svg%3E") ${center} ${center}, auto`
  }
}
