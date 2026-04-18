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

<script setup>
import { computed, ref, watch } from 'vue'
import ControlSlider from '@/components/ControlSlider.vue'
import player from '@/api/player'
import { usePlayerStore } from '@/stores/player'

const playerStore = usePlayerStore()

const volume = ref(0)

const icon = computed(
  () => (playerStore.isMuted && 'volume-off') || 'volume-high'
)

watch(
  () => playerStore.volume,
  (newVolume) => {
    if (!playerStore.isMuted) {
      volume.value = newVolume
    }
  }
)

const changeVolume = () => {
  player.setVolume(playerStore.volume)
}

const toggle = () => {
  playerStore.isMuted = (playerStore.isMuted && volume.value) || 0
  changeVolume()
}
</script>
