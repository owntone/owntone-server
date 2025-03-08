<template>
  <modal-dialog :actions="actions" :show="show" @close="$emit('close')">
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
import webapi from '@/webapi'

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
        { handler: this.addToQueue, icon: 'playlist-plus', key: 'actions.add' },
        {
          handler: this.addNextToQueue,
          icon: 'playlist-play',
          key: 'actions.add-next'
        },
        { handler: this.play, icon: 'play', key: 'actions.play' }
      ]
    }
  },
  methods: {
    play() {
      this.$emit('close')
      if (this.item.expression) {
        webapi.player_play_expression(this.item.expression, false)
      } else {
        webapi.player_play_uri(this.item.uris || this.item.uri, false)
      }
    },
    addToQueue() {
      this.$emit('close')
      if (this.item.expression) {
        webapi.queue_expression_add(this.item.expression)
      } else {
        webapi.queue_add(this.item.uris || this.item.uri)
      }
    },
    addNextToQueue() {
      this.$emit('close')
      if (this.item.expression) {
        webapi.queue_expression_add_next(this.item.expression)
      } else {
        webapi.queue_add_next(this.item.uris || this.item.uri)
      }
    }
  }
}
</script>
