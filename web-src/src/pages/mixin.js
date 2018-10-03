
export const LoadDataBeforeEnterMixin = function (dataObject) {
  return {
    beforeRouteEnter (to, from, next) {
      dataObject.load(to).then((response) => {
        next(vm => dataObject.set(vm, response))
      })
    },
    beforeRouteUpdate (to, from, next) {
      const vm = this
      dataObject.load(to).then((response) => {
        dataObject.set(vm, response)
        next()
      })
    }
  }
}
