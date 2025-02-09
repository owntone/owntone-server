<template>
  <modal-dialog :actions="actions" :show="show" @close="$emit('close')">
    <template #content>
      <slot name="content" />
    </template>
  </modal-dialog>
</template>

<script>
import ModalDialog from '@/components/ModalDialog.vue'
import webapi from '@/webapi'

export default {
  name: 'ModalDialogPlayable',
  components: { ModalDialog },
  props: {
    expression: { default: '', type: String },
    item: {
      default() {
        return {}
      },
      type: Object
    },
    show: Boolean
  },
  emits: ['close'],
  computed: {
    actions() {
      return [
        {
          label: this.$t('dialog.playable.add'),
          handler: this.queue_add,
          icon: 'playlist-plus'
        },
        {
          label: this.$t('dialog.playable.add-next'),
          handler: this.queue_add_next,
          icon: 'playlist-play'
        },
        {
          label: this.$t('dialog.playable.play'),
          handler: this.play,
          icon: 'play'
        }
      ]
    }
  },
  methods: {
    play() {
      this.$emit('close')
      if (this.expression) {
        webapi.player_play_expression(this.expression, false)
      } else {
        webapi.player_play_uri(this.item.uri, false)
      }
    },
    queue_add() {
      this.$emit('close')
      if (this.expression) {
        webapi.queue_expression_add(this.expression)
      } else {
        webapi.queue_add(this.item.uri)
      }
    },
    queue_add_next() {
      this.$emit('close')
      if (this.expression) {
        webapi.queue_expression_add_next(this.expression)
      } else {
        webapi.queue_add_next(this.item.uri)
      }
    }
  }
}
</script>
