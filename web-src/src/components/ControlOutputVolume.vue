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

<script>
import ControlSlider from '@/components/ControlSlider.vue'
import webapi from '@/webapi'

export default {
  name: 'ControlOutputVolume',
  components: {
    ControlSlider
  },
  props: { output: { required: true, type: Object } },

  data() {
    return {
      volume: this.output.selected ? this.output.volume : 0
    }
  },

  computed: {
    icon() {
      if (this.output.type.startsWith('AirPlay')) {
        return 'cast-variant'
      } else if (this.output.type === 'Chromecast') {
        return 'cast'
      } else if (this.output.type === 'fifo') {
        return 'pipe'
      }
      return 'server'
    }
  },

  watch: {
    output() {
      this.volume = this.output.volume
    }
  },

  methods: {
    changeVolume() {
      webapi.player_output_volume(this.output.id, this.volume)
    },
    toggle() {
      const values = {
        selected: !this.output.selected
      }
      webapi.output_update(this.output.id, values)
    }
  }
}
</script>
