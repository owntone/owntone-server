<template>
  <a :href="$router.resolve(to).href" @click.stop.prevent="open">
    <slot />
  </a>
</template>

<script setup>
import { useRouter } from 'vue-router'
import { useUIStore } from '@/stores/ui'

const props = defineProps({ to: { required: true, type: Object } })

const uiStore = useUIStore()
const router = useRouter()

const open = () => {
  uiStore.hideMenus()
  router.push(props.to)
}
</script>
