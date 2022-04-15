import { DateTime, Duration } from 'luxon'

export const filters = {
  durationInHours: function (value_ms) {
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

  durationInDays: function (value_ms) {
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

  date: function (value) {
    return DateTime.fromISO(value).toLocaleString(DateTime.DATE_FULL)
  },

  datetime: function (value) {
    return DateTime.fromISO(value).toLocaleString(DateTime.DATETIME_MED)
  },

  timeFromNow: function (value) {
    var diff = DateTime.now().diff(DateTime.fromISO(value))

    return this.durationInDays(diff.as('milliseconds'))
  },

  number: function (value) {
    return value.toLocaleString()
  },

  channels: function (value) {
    if (value === 1) {
      return 'mono'
    }
    if (value === 2) {
      return 'stereo'
    }
    if (!value) {
      return ''
    }
    return value + ' channels'
  }
}
