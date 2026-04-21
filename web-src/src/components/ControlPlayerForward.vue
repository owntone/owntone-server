<template>
  <button v-if="visible" :disabled="queueStore.isEmpty" @click="seek">
    <mdicon
      class="icon"
      name="fast-forward-30"
      :title="$t('player.button.seek-forward')"
    />
  </button>
</template>

<script setup>
import { computed } from 'vue'
import player from '@/api/player'
import { useQueueStore } from '@/stores/queue'

const props = defineProps({ offset: { required: true, type: Number } })

const queueStore = useQueueStore()

const visible = computed(() =>
  ['podcast', 'audiobook'].includes(queueStore.current?.media_kind)
)

const seek = () => {
  player.seek(props.offset)
}
</script>
