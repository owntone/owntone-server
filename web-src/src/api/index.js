import api from 'axios'
import i18n from '@/i18n'
import { useNotificationsStore } from '@/stores/notifications'

const { t } = i18n.global

api.interceptors.response.use(
  (response) => response.data,
  (error) => {
    if (error.request.status && error.request.responseURL) {
      useNotificationsStore().add({
        text: t('server.request-failed', {
          cause: error.request.statusText,
          status: error.request.status,
          url: error.request.responseURL
        }),
        type: 'danger'
      })
    }
    return Promise.reject(error)
  }
)

export default api
