import api from '@/api'

const BASE_URL = './api/outputs'

export default {
  state() {
    return api.get(BASE_URL)
  },
  toggle(id) {
    return api.put(`${BASE_URL}/${id}/toggle`)
  },
  update(id, output) {
    return api.put(`${BASE_URL}/${id}`, output)
  }
}
