<template>
  <section v-if="!notificationsStore.isEmpty" class="notifications">
    <div class="columns is-centered">
      <div class="column is-half">
        <div
          v-for="(notification, index) in notificationsStore.list"
          :key="index"
          class="notification"
          :class="notification.type ? `is-${notification.type}` : ''"
        >
          <button
            class="delete"
            @click="notificationsStore.remove(notification)"
          />
          <div class="text" v-text="notification.text" />
        </div>
      </div>
    </div>
  </section>
</template>

<script>
import { useNotificationsStore } from '@/stores/notifications'

export default {
  name: 'ListNotifications',
  setup() {
    return { notificationsStore: useNotificationsStore() }
  }
}
</script>

<style scoped>
.notifications {
  position: fixed;
  bottom: 4rem;
  z-index: 20000;
  width: 100%;
}
.notifications .notification {
  box-shadow:
    0 4px 8px 0 rgba(0, 0, 0, 0.2),
    0 6px 20px 0 rgba(0, 0, 0, 0.19);
}
.notification .text {
  overflow-wrap: break-word;
  max-height: 6rem;
  overflow: scroll;
}
</style>
