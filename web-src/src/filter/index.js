import { DateTime, Duration } from 'luxon'
import i18n from '@/i18n'

const { locale } = i18n.global

export const filters = {
  date(value) {
    if (value) {
      return DateTime.fromISO(value, { locale: locale.value }).toLocaleString(
        DateTime.DATE_FULL
      )
    }
    return null
  },
  datetime(value) {
    if (value) {
      return DateTime.fromISO(value, { locale: locale.value }).toLocaleString(
        DateTime.DATETIME_MED
      )
    }
    return null
  },
  duration(value) {
    const diff = DateTime.now().diff(DateTime.fromISO(value))
    return this.durationInDays(diff.as('seconds'))
  },
  durationInDays(value) {
    const minutes = Math.floor(value / 60)
    if (minutes > 1440) {
      return Duration.fromObject({ minutes })
        .shiftTo('days', 'hours', 'minutes')
        .toHuman()
    } else if (minutes > 60) {
      return Duration.fromObject({ minutes })
        .shiftTo('hours', 'minutes')
        .toHuman()
    }
    return Duration.fromObject({ minutes }).shiftTo('minutes').toHuman()
  },
  durationInHours(value) {
    const format = value >= 3600000 ? 'h:mm:ss' : 'm:ss'
    return Duration.fromMillis(value).toFormat(format)
  },
  timeFromNow(value) {
    return DateTime.fromISO(value).toRelative({
      unit: ['years', 'months', 'days', 'hours', 'minutes', 'seconds']
    })
  }
}
