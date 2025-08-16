<template>
  <tabs-music />
  <content-with-heading v-for="[type, items] in results" :key="type">
    <template #heading>
      <pane-title :content="{ title: $t(types[type].title) }" />
    </template>
    <template #content>
      <component
        :is="types[type].component"
        :items="items"
        :load="(expanded && ((args) => load(type, args))) || null"
      />
    </template>
    <template v-if="!expanded" #footer>
      <control-button
        :button="{
          handler: () => load(type),
          title: $t('actions.show-more')
        }"
      />
    </template>
  </content-with-heading>
</template>

<script>
import ContentWithHeading from '@/templates/ContentWithHeading.vue'
import ControlButton from '@/components/ControlButton.vue'
import PaneTitle from '@/components/PaneTitle.vue'
import TabsMusic from '@/components/TabsMusic.vue'

const PAGE_SIZE_EXPANDED = 50

export default {
  name: 'ContentWithLists',
  components: { ContentWithHeading, ControlButton, PaneTitle, TabsMusic },
  props: {
    results: { required: true, type: Object },
    types: { required: true, type: Object }
  },
  data() {
    return {
      expanded: false
    }
  },
  methods: {
    load(type, { loaded } = {}) {
      this.expanded = true
      this.types[type].handler(PAGE_SIZE_EXPANDED, this.expanded, loaded)
    }
  }
}
</script>
