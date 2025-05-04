import api from '@/api'

export default {
  pair(pin) {
    return api.post('./api/pairing', { pin })
  },
  state() {
    return api.get('./api/pairing')
  }
}
