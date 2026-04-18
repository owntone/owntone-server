<template>
  <div class="media is-align-items-center mb-0">
    <div class="media-left">
      <button
        class="button is-small"
        :class="{ 'has-text-grey-light': !output.selected }"
        @click="toggle"
      >
        <mdicon class="icon" :name="icon" :title="output.type" />
      </button>
    </div>
    <div class="media-content">
      <div
        class="is-size-7 is-uppercase"
        :class="{ 'has-text-grey-light': !output.selected }"
        v-text="output.name"
      />
      <control-slider
        v-model:value="volume"
        :disabled="!output.selected"
        :max="100"
        @change="changeVolume"
      />
    </div>
  </div>
</template>

<script setup>
import { computed, ref, watch } from 'vue'

import ControlSlider from '@/components/ControlSlider.vue'
import outputs from '@/api/outputs'
import player from '@/api/player'

const props = defineProps({ output: { required: true, type: Object } })

const volume = ref(props.output.volume)

const icon = computed(() => {
  if (props.output.type.startsWith('AirPlay')) {
    return 'cast-variant'
  } else if (props.output.type === 'Chromecast') {
    return 'cast'
  } else if (props.output.type === 'fifo') {
    return 'pipe'
  }
  return 'server'
})

watch(
  () => props.output,
  (newOutput) => {
    volume.value = newOutput.volume
  }
)

const changeVolume = () => {
  player.setVolume(volume.value, props.output.id)
}

const toggle = () => {
  outputs.update(props.output.id, { selected: !props.output.selected })
}
</script>
