import api from '@/api'
import i18n from '@/i18n'
import { useNotificationsStore } from '@/stores/notifications'
import { useQueueStore } from '@/stores/queue'

const { t } = i18n.global

export default {
  addUri(uris, next = false) {
    return this.addToQueue({ uris }, next)
  },
  addExpression(expression, next = false) {
    return this.addToQueue({ expression }, next)
  },
  async addToQueue(params, next = false) {
    if (next) {
      const { current } = useQueueStore()
      if (current?.id) {
        params.position = current.position + 1
      }
    }
    const data = await api.post('./api/queue/items/add', null, { params })
    useNotificationsStore().add({
      text: t('server.appended-tracks', { count: data.count }),
      timeout: 2000,
      type: 'info'
    })
    return data
  },
  clear() {
    return api.put('./api/queue/clear')
  },
  move(id, position) {
    return api.put(`./api/queue/items/${id}?new_position=${position}`)
  },
  playExpression(expression, shuffle, position) {
    const params = {
      clear: 'true',
      expression,
      playback: 'start',
      playback_from_position: position,
      shuffle
    }
    return api.post('./api/queue/items/add', null, { params })
  },
  playUri(uris, shuffle, position) {
    const params = {
      clear: 'true',
      playback: 'start',
      playback_from_position: position,
      shuffle,
      uris
    }
    return api.post('./api/queue/items/add', null, { params })
  },
  remove(id) {
    return api.delete(`./api/queue/items/${id}`)
  },
  state() {
    return api.get('./api/queue')
  },
  async saveToPlaylist(name) {
    const response = await api.post('./api/queue/save', null, {
      params: { name }
    })
    useNotificationsStore().add({
      text: t('server.queue-saved', { name }),
      timeout: 2000,
      type: 'info'
    })
    return await Promise.resolve(response)
  }
}
