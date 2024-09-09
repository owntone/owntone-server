<template>
  <div class="media is-align-items-center pt-0">
    <div class="media-left">
      <a class="button is-white is-small" @click="toggle">
        <mdicon class="icon" :name="icon" />
      </a>
    </div>
    <div class="media-content">
      <p class="heading" v-text="$t('navigation.volume')" />
      <control-slider
        v-model:value="player.volume"
        :cursor="cursor"
        :max="100"
        @change="changeVolume"
      />
    </div>
  </div>
</template>

<script>
import ControlSlider from '@/components/ControlSlider.vue'
import { mdiCancel } from '@mdi/js'
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
      cursor: mdiCancel,
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
    changeVolume(value) {
      webapi.player_volume(this.player.volume)
    },
    toggle() {
      this.player.volume = this.player.volume > 0 ? 0 : this.old_volume
      this.changeVolume()
    }
  }
}
</script>
