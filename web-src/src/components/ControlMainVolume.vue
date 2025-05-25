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
        v-model:value="playerStore.volume"
        :max="100"
        @change="changeVolume"
      />
    </div>
  </div>
</template>

<script>
import ControlSlider from '@/components/ControlSlider.vue'
import player from '@/api/player'
import { usePlayerStore } from '@/stores/player'

export default {
  name: 'ControlVolume',
  components: { ControlSlider },
  setup() {
    return { playerStore: usePlayerStore() }
  },
  data() {
    return { volume: 0 }
  },
  computed: {
    icon() {
      if (this.playerStore.isMuted) {
        return 'volume-off'
      }
      return 'volume-high'
    }
  },
  watch: {
    'playerStore.volume'() {
      if (!this.playerStore.isMuted) {
        this.volume = this.playerStore.volume
      }
    }
  },
  methods: {
    changeVolume() {
      player.setVolume(this.playerStore.volume)
    },
    toggle() {
      if (this.playerStore.isMuted) {
        this.playerStore.volume = this.volume
      } else {
        this.playerStore.volume = 0
      }
      this.changeVolume()
    }
  }
}
</script>
