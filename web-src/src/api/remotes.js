import api from '@/api'

const BASE_URL = './api/pairing'

export default {
  pair(pin) {
    return api.post(BASE_URL, { pin })
  },
  state() {
    return api.get(BASE_URL)
  }
}
