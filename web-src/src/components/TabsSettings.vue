<template>
  <control-tab-list :links="links" />
</template>

<script setup>
import ControlTabList from '@/components/ControlTabList.vue'
import { computed } from 'vue'

const links = computed(() => [
  {
    key: 'page.settings.tabs.webinterface',
    to: { name: 'settings-webinterface' }
  },
  { key: 'page.settings.tabs.devices', to: { name: 'settings-devices' } },
  { key: 'page.settings.tabs.artwork', to: { name: 'settings-artwork' } },
  {
    key: 'page.settings.tabs.online-services',
    to: { name: 'settings-online-services' }
  }
])
</script>
