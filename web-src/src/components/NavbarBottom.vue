<template>
  <nav class="navbar is-fixed-bottom" :class="{ 'is-bottom': !isPlayerPage }">
    <div class="navbar-brand is-flex-grow-1">
      <control-link class="navbar-item" :to="{ name: 'queue' }">
        <mdicon class="icon" name="playlist-play" />
      </control-link>
      <template v-if="isPlayerPage">
        <control-player-previous class="navbar-item ml-auto" />
        <control-player-back class="navbar-item" :offset="10000" />
        <control-player-play class="navbar-item" />
        <control-player-forward class="navbar-item" :offset="30000" />
        <control-player-next class="navbar-item mr-auto" />
      </template>
      <template v-else>
        <control-link
          :to="{ name: 'player' }"
          exact
          class="navbar-item is-justify-content-flex-start is-expanded is-clipped is-size-7"
        >
          <div class="is-text-clipped">
            <strong v-text="queueStore.current.title" />
            <br />
            <span v-text="metadata" />
          </div>
        </control-link>
        <control-player-play class="navbar-item" />
      </template>
      <a class="navbar-item" @click="uiStore.togglePlayerMenu">
        <mdicon
          class="icon"
          :name="uiStore.showPlayerMenu ? 'chevron-down' : 'chevron-up'"
        />
      </a>
      <div
        class="dropdown is-up is-right"
        :class="{ 'is-active': uiStore.showPlayerMenu }"
      >
        <div class="dropdown-menu is-mobile">
          <div class="dropdown-content">
            <div class="dropdown-item pt-0">
              <control-main-volume />
              <control-output-volume
                v-for="output in outputsStore.outputs"
                :key="output.id"
                :output="output"
              />
              <control-stream-volume />
            </div>
            <hr class="dropdown-divider" />
            <div class="dropdown-item is-flex is-justify-content-center">
              <div class="buttons has-addons">
                <control-player-repeat class="button" />
                <control-player-shuffle class="button" />
                <control-player-consume class="button" />
                <control-player-lyrics class="button" />
              </div>
            </div>
          </div>
        </div>
      </div>
    </div>
  </nav>
</template>

<script setup>
import ControlLink from '@/components/ControlLink.vue'
import ControlMainVolume from '@/components/ControlMainVolume.vue'
import ControlOutputVolume from '@/components/ControlOutputVolume.vue'
import ControlPlayerBack from '@/components/ControlPlayerBack.vue'
import ControlPlayerConsume from '@/components/ControlPlayerConsume.vue'
import ControlPlayerForward from '@/components/ControlPlayerForward.vue'
import ControlPlayerLyrics from '@/components/ControlPlayerLyrics.vue'
import ControlPlayerNext from '@/components/ControlPlayerNext.vue'
import ControlPlayerPlay from '@/components/ControlPlayerPlay.vue'
import ControlPlayerPrevious from '@/components/ControlPlayerPrevious.vue'
import ControlPlayerRepeat from '@/components/ControlPlayerRepeat.vue'
import ControlPlayerShuffle from '@/components/ControlPlayerShuffle.vue'
import ControlStreamVolume from '@/components/ControlStreamVolume.vue'
import { computed } from 'vue'
import { useOutputsStore } from '@/stores/outputs'
import { useQueueStore } from '@/stores/queue'
import { useRoute } from 'vue-router'
import { useUIStore } from '@/stores/ui'

const route = useRoute()
const outputsStore = useOutputsStore()
const queueStore = useQueueStore()
const uiStore = useUIStore()

const isPlayerPage = computed(() => route.name === 'player')

const metadata = computed(() => {
  const { current } = queueStore
  return [current.artist, current.album].filter(Boolean).join(' - ')
})
</script>

<style scoped>
.is-text-clipped {
  white-space: nowrap;
  overflow: hidden;
  text-overflow: ellipsis;
}
</style>
