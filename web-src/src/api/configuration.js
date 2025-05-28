import api from '@/api'

export default {
  state() {
    return api.get('./api/config')
  }
}
