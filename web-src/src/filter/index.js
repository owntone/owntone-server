import moment from 'moment'
import momentDurationFormatSetup from 'moment-duration-format'

momentDurationFormatSetup(moment)

export const filters = {
  duration: function (value, format) {
    if (format) {
      return moment.duration(value).format(format)
    }
    return moment.duration(value).format('hh:*mm:ss')
  },

  time: function (value, format) {
    if (format) {
      return moment(value).format(format)
    }
    return moment(value).format()
  },

  timeFromNow: function (value, withoutSuffix) {
    return moment(value).fromNow(withoutSuffix)
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
