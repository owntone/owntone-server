import { DateTime, Duration } from 'luxon'
import i18n from '@/i18n'

const { locale } = i18n.global
const unit = ['years', 'months', 'days', 'hours', 'minutes']

export const filters = {
  toDate(value) {
    if (value) {
      return DateTime.fromISO(value, { locale: locale.value }).toLocaleString(
        DateTime.DATE_FULL
      )
    }
    return null
  },
  toDateTime(value) {
    if (value) {
      return DateTime.fromISO(value, { locale: locale.value }).toLocaleString(
        DateTime.DATETIME_MED
      )
    }
    return null
  },
  toDuration(seconds) {
    const shifted = Duration.fromObject({
      minutes: Math.floor(seconds / 60)
    }).shiftTo(...unit)
    const filtered = Object.fromEntries(
      Object.entries(shifted.toObject()).filter(([, value]) => value > 0)
    )
    return Duration.fromObject(filtered, { locale: locale.value }).toHuman()
  },
  toDurationToNow(value) {
    const duration = DateTime.now().diff(DateTime.fromISO(value)).as('seconds')
    return this.toDuration(duration)
  },
  toRelativeDuration(value) {
    return DateTime.fromISO(value).toRelative({ locale: locale.value, unit })
  },
  toTimecode(value) {
    return Duration.fromMillis(value).toFormat('h:mm:ss').replace(/^0:/u, '')
  }
}
