import { defineStore } from 'pinia'

export const useNotificationsStore = defineStore('NotificationsStore', {
  actions: {
    add(notification) {
      const newNotification = {
        id: this.next_id++,
        text: notification.text,
        timeout: notification.timeout,
        topic: notification.topic,
        type: notification.type
      }
      if (newNotification.topic) {
        const index = this.list.findIndex(
          (elem) => elem.topic === newNotification.topic
        )
        if (index >= 0) {
          this.list.splice(index, 1, newNotification)
          return
        }
      }
      this.list.push(newNotification)
      if (notification.timeout > 0) {
        setTimeout(() => {
          this.remove(newNotification)
        }, notification.timeout)
      }
    },
    remove(notification) {
      const index = this.list.indexOf(notification)
      if (index !== -1) {
        this.list.splice(index, 1)
      }
    }
  },
  state: () => ({
    list: [],
    next_id: 1
  })
})
