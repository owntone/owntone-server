<template>
  <a :href="$router.resolve(to).href" @click.stop.prevent="open">
    <slot />
  </a>
</template>

<script>
import { useUIStore } from '@/stores/ui'

export default {
  name: 'ControlLink',
  props: {
    to: { required: true, type: Object }
  },
  setup() {
    return { uiStore: useUIStore() }
  },
  methods: {
    open() {
      this.uiStore.hideMenus()
      this.$router.push(this.to)
    }
  }
}
</script>
