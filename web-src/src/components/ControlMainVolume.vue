<template>
  <div class="media is-align-items-center mb-0">
    <div class="media-left">
      <button class="button is-small" @click="toggle">
        <mdicon class="icon" :name="icon" />
      </button>
    </div>
    <div class="media-content">
      <div class="is-size-7 is-uppercase" v-text="$t('navigation.volume')" />
      <control-slider
        v-model:value="player.volume"
        :max="100"
        @change="changeVolume"
      />
    </div>
  </div>
</template>

<script>
import ControlSlider from '@/components/ControlSlider.vue'
import { usePlayerStore } from '@/stores/player'
import webapi from '@/webapi'

export default {
  name: 'ControlVolume',
  components: { ControlSlider },
  setup() {
    return {
      player: usePlayerStore()
    }
  },
  data() {
    return {
      old_volume: 0
    }
  },
  computed: {
    icon() {
      return this.player.volume > 0 ? 'volume-high' : 'volume-off'
    }
  },
  watch: {
    'player.volume'() {
      if (this.player.volume > 0) {
        this.old_volume = this.player.volume
      }
    }
  },
  methods: {
    changeVolume() {
      webapi.player_volume(this.player.volume)
    },
    toggle() {
      this.player.volume = this.player.volume > 0 ? 0 : this.old_volume
      this.changeVolume()
    }
  }
}
</script>
