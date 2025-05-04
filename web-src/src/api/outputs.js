import api from '@/api'

export default {
  toggle(id) {
    return api.put(`./api/outputs/${id}/toggle`)
  },
  state() {
    return api.get('./api/outputs')
  },
  update(id, output) {
    return api.put(`./api/outputs/${id}`, output)
  }
}
