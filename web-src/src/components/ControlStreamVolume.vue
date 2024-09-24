<template>
  <div class="media is-align-items-center pt-0">
    <div class="media-left">
      <a
        class="button is-small"
        :class="{
          'has-text-grey-light': !playing && !loading,
          'is-loading': loading
        }"
        @click="togglePlay"
      >
        <mdicon class="icon" name="broadcast" />
      </a>
    </div>
    <div class="media-content">
      <div
        class="is-flex is-align-content-center"
        :class="{ 'has-text-grey-light': !playing }"
      >
        <p class="heading" v-text="$t('navigation.stream')" />
        <a href="stream.mp3" class="heading ml-2" target="_blank">
          <mdicon class="icon is-small" name="open-in-new" />
        </a>
      </div>
      <control-slider
        v-model:value="volume"
        :cursor="cursor"
        :disabled="!playing"
        :max="100"
        @change="changeVolume"
      />
    </div>
  </div>
</template>

<script>
import ControlSlider from '@/components/ControlSlider.vue'
import audio from '@/lib/Audio'
import { mdiCancel } from '@mdi/js'

export default {
  name: 'ControlStreamVolume',
  components: { ControlSlider },
  emits: ['change', 'mute'],
  data() {
    return {
      cursor: mdiCancel,
      loading: false,
      playing: false,
      volume: 10
    }
  },
  mounted() {
    this.setupAudio()
  },
  unmounted() {
    this.closeAudio()
  },
  methods: {
    changeVolume() {
      audio.setVolume(this.volume / 100)
    },
    closeAudio() {
      audio.stop()
      this.playing = false
    },
    playChannel() {
      if (this.playing) {
        return
      }
      this.loading = true
      audio.play('/stream.mp3')
      audio.setVolume(this.volume / 100)
    },
    setupAudio() {
      const a = audio.setup()
      a.addEventListener('waiting', () => {
        this.playing = false
        this.loading = true
      })
      a.addEventListener('playing', () => {
        this.playing = true
        this.loading = false
      })
      a.addEventListener('ended', () => {
        this.playing = false
        this.loading = false
      })
      a.addEventListener('error', () => {
        this.closeAudio()
        this.notificationsStore.add({
          text: this.$t('navigation.stream-error'),
          type: 'danger'
        })
        this.playing = false
        this.loading = false
      })
    },
    togglePlay() {
      if (this.loading) {
        return
      }
      if (this.playing) {
        this.closeAudio()
      }
      this.playChannel()
    }
  }
}
</script>
