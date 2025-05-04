import api from '@/api'

export default {
  state() {
    return api.get('./api/settings')
  },
  update(categoryName, option) {
    return api.put(`./api/settings/${categoryName}/${option.name}`, option)
  }
}
