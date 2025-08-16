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

<script>
import ControlButton from '@/components/ControlButton.vue'
import ListProperties from '@/components/ListProperties.vue'
import ModalDialog from '@/components/ModalDialog.vue'
import queue from '@/api/queue'

export default {
  name: 'ModalDialogPlayable',
  components: { ControlButton, ListProperties, ModalDialog },
  props: {
    buttons: { default: () => [], type: Array },
    item: { required: true, type: Object },
    show: Boolean
  },
  emits: ['close'],
  computed: {
    actions() {
      return [
        {
          disabled: !this.item.playable,
          handler: this.addToQueue,
          icon: 'playlist-plus',
          key: 'actions.add'
        },
        {
          disabled: !this.item.playable,
          handler: this.addNextToQueue,
          icon: 'playlist-play',
          key: 'actions.add-next'
        },
        {
          disabled: !this.item.playable,
          handler: this.play,
          icon: 'play',
          key: 'actions.play'
        }
      ]
    }
  },
  methods: {
    addNextToQueue() {
      this.$emit('close')
      if (this.item.expression) {
        queue.addExpression(this.item.expression, true)
      } else {
        queue.addUri(this.item.uris || this.item.uri, true)
      }
    },
    addToQueue() {
      this.$emit('close')
      if (this.item.expression) {
        queue.addExpression(this.item.expression)
      } else {
        queue.addUri(this.item.uris || this.item.uri)
      }
    },
    play() {
      this.$emit('close')
      if (this.item.expression) {
        queue.playExpression(this.item.expression, false)
      } else {
        queue.playUri(this.item.uris || this.item.uri, false)
      }
    }
  }
}
</script>
