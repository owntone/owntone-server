<template>
  <content-with-heading>
    <template #heading>
      <pane-title :content="{ title: $t('page.outputs.title') }" />
    </template>
    <template #content>
      <control-main-volume />
      <control-output-volume
        v-for="output in outputsStore.outputs"
        :key="output.id"
        :output="output"
      />
      <control-stream-volume />
    </template>
  </content-with-heading>
</template>

<script>
import ContentWithHeading from '@/templates/ContentWithHeading.vue'
import ControlMainVolume from '@/components/ControlMainVolume.vue'
import ControlOutputVolume from '@/components/ControlOutputVolume.vue'
import ControlStreamVolume from '@/components/ControlStreamVolume.vue'
import PaneTitle from '@/components/PaneTitle.vue'
import { useOutputsStore } from '@/stores/outputs'

export default {
  name: 'PageOutputs',
  components: {
    ContentWithHeading,
    ControlMainVolume,
    ControlOutputVolume,
    ControlStreamVolume,
    PaneTitle
  },
  setup() {
    return {
      outputsStore: useOutputsStore()
    }
  }
}
</script>
