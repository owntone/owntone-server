import api from '@/api'

export default {
  list() {
    return api.get('./api/config')
  }
}
