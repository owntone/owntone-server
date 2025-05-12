import api from '@/api'

const BASE_URL = './api/outputs'

export default {
  toggle(id) {
    return api.put(`${BASE_URL}/${id}/toggle`)
  },
  state() {
    return api.get(BASE_URL)
  },
  update(id, output) {
    return api.put(`${BASE_URL}/${id}`, output)
  }
}
