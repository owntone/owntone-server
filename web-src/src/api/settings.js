import api from '@/api'

const BASE_URL = './api/settings'

export default {
  state() {
    return api.get(BASE_URL)
  },
  update(option) {
    return api.put(`${BASE_URL}/${option.category}/${option.name}`, option)
  }
}
