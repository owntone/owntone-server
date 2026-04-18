<template>
  <div class="media is-align-items-center mb-0">
    <div class="media-left">
      <button
        class="button is-small"
        :class="{
          'has-text-grey-light': !playing && !loading,
          'is-loading': loading
        }"
        @click="togglePlay"
      >
        <mdicon class="icon" name="broadcast" />
      </button>
    </div>
    <div class="media-content is-align-items-center">
      <div class="is-flex" :class="{ 'has-text-grey-light': !playing }">
        <div class="is-size-7 is-uppercase" v-text="$t('navigation.stream')" />
        <a href="stream.mp3" class="ml-2" target="_blank">
          <mdicon class="icon is-small" name="open-in-new" />
        </a>
      </div>
      <control-slider
        v-model:value="volume"
        :disabled="!playing"
        :max="100"
        @change="changeVolume"
      />
    </div>
  </div>
</template>

<script setup>
import { onUnmounted, ref } from 'vue'
import ControlSlider from '@/components/ControlSlider.vue'
import audio from '@/lib/Audio'

const loading = ref(false)
const playing = ref(false)
const volume = ref(10)

const changeVolume = () => {
  audio.setVolume(volume.value / 100)
}

const closeAudio = () => {
  audio.stop()
  playing.value = false
  loading.value = false
}

const play = () => {
  loading.value = true
  audio.play('/stream.mp3')
  changeVolume()
  const a = audio.audio
  if (!a) {
    return
  }
  a.addEventListener('waiting', () => {
    playing.value = false
    loading.value = true
  })
  a.addEventListener('playing', () => {
    playing.value = true
    loading.value = false
  })
  a.addEventListener('ended', () => {
    playing.value = false
    loading.value = false
  })
  a.addEventListener('error', () => {
    closeAudio()
  })
}

const togglePlay = () => {
  if (playing.value || loading.value) {
    closeAudio()
  } else {
    play()
  }
}

onUnmounted(() => {
  closeAudio()
})
</script>
