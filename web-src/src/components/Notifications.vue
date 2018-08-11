<template>
  <section class="fd-notifications">
    <div class="columns is-centered">
      <div class="column is-half">
        <div class="notification has-shadow " v-for="notification in notifications" :key="notification.id" :class="['notification', notification.type ? `is-${notification.type}` : '']">
          <button class="delete" v-on:click="remove(notification)"></button>
          {{ notification.text }}
        </div>
      </div>
    </div>
  </section>
</template>

<script>
import * as types from '@/store/mutation_types'

export default {
  name: 'Notifications',
  components: { },

  data () {
    return { showNav: false }
  },

  computed: {
    notifications () {
      return this.$store.state.notifications.list
    }
  },

  methods: {
    remove: function (notification) {
      this.$store.commit(types.DELETE_NOTIFICATION, notification)
    }
  }
}
</script>

<style>
.fd-notifications {
  position: fixed;
  bottom: 60px;
  z-index: 20000;
  width: 100%;
}
.fd-notifications .notification {
  margin-bottom: 10px;
  margin-left: 24px;
  margin-right: 24px;
  box-shadow: 0 4px 8px 0 rgba(0, 0, 0, 0.2), 0 6px 20px 0 rgba(0, 0, 0, 0.19);
}
</style>
