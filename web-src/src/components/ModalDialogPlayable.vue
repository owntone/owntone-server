<template>
  <modal-dialog
    :actions="actions"
    :show="show"
    :title="item.name"
    @close="$emit('close')"
  >
    <template #content>
      <list-properties :item="item">
        <template v-if="buttons.length" #buttons>
          <div class="buttons">
            <control-button
              v-for="button in buttons"
              :key="button.key"
              :button="button"
            />
          </div>
        </template>
      </list-properties>
    </template>
  </modal-dialog>
</template>

<script setup>
import ControlButton from '@/components/ControlButton.vue'
import ListProperties from '@/components/ListProperties.vue'
import ModalDialog from '@/components/ModalDialog.vue'
import queue from '@/api/queue'

const props = defineProps({
  buttons: { default: () => [], type: Array },
  item: { required: true, type: Object },
  show: Boolean
})

const emit = defineEmits(['close'])

const resolveSource = () =>
  props.item.expression || props.item.uris || props.item.uri

const addNextToQueue = () => {
  emit('close')
  if (props.item.expression) {
    queue.addExpression(props.item.expression, true)
  } else {
    queue.addUri(resolveSource(), true)
  }
}

const addToQueue = () => {
  emit('close')
  if (props.item.expression) {
    queue.addExpression(props.item.expression)
  } else {
    queue.addUri(resolveSource())
  }
}

const play = () => {
  emit('close')
  if (props.item.expression) {
    queue.playExpression(props.item.expression, false)
  } else {
    queue.playUri(resolveSource(), false)
  }
}

const actions = [
  { handler: addToQueue, icon: 'playlist-plus', key: 'actions.add' },
  { handler: addNextToQueue, icon: 'playlist-play', key: 'actions.add-next' },
  { handler: play, icon: 'play', key: 'actions.play' }
]
</script>
