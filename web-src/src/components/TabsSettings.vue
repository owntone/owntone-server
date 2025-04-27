<template>
  <control-tab-list :links="links" />
</template>

<script>
import ControlTabList from '@/components/ControlTabList.vue'

export default {
  name: 'TabsSettings',
  components: { ControlTabList },
  computed: {
    links() {
      return [
        {
          key: 'page.settings.tabs.general',
          to: { name: 'settings-webinterface' }
        },
        {
          key: 'page.settings.tabs.devices',
          to: { name: 'settings-devices' }
        },
        {
          key: 'page.settings.tabs.artwork',
          to: { name: 'settings-artwork' }
        },
        {
          key: 'page.settings.tabs.online-services',
          to: { name: 'settings-online-services' }
        }
      ]
    }
  }
}
</script>
