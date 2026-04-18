<template>
  <button v-if="visible" :disabled="disabled" @click="seek">
    <mdicon
      class="icon"
      name="rewind-10"
      :title="$t('player.button.seek-backward')"
    />
  </button>
</template>

<script setup>
import { computed } from 'vue'
import player from '@/api/player'
import { usePlayerStore } from '@/stores/player'
import { useQueueStore } from '@/stores/queue'

const props = defineProps({ offset: { type: Number, required: true } })

const playerStore = usePlayerStore()
const queueStore = useQueueStore()

const disabled = computed(() => queueStore.isEmpty || playerStore.isStopped)

const visible = computed(() =>
  ['podcast', 'audiobook'].includes(queueStore.current?.media_kind)
)

const seek = () => {
  player.seek(props.offset * -1)
}
</script>
