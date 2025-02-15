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
          to: { name: 'settings-webinterface' },
          label: 'page.settings.tabs.general'
        },
        {
          to: { name: 'settings-remotes-outputs' },
          label: 'page.settings.tabs.remotes-and-outputs'
        },
        {
          to: { name: 'settings-artwork' },
          label: 'page.settings.tabs.artwork'
        },
        {
          to: { name: 'settings-online-services' },
          label: 'page.settings.tabs.online-services'
        }
      ]
    }
  }
}
</script>
