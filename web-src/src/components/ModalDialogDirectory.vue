<template>
  <modal-dialog
    :actions="actions"
    :show="show"
    :title="item"
    @close="$emit('close')"
  />
</template>

<script>
import ModalDialog from '@/components/ModalDialog.vue'
import webapi from '@/webapi'

export default {
  name: 'ModalDialogDirectory',
  components: { ModalDialog },
  props: { item: { required: true, type: String }, show: Boolean },
  emits: ['close'],
  computed: {
    actions() {
      return [
        {
          label: this.$t('dialog.directory.add'),
          handler: this.queue_add,
          icon: 'playlist-plus'
        },
        {
          label: this.$t('dialog.directory.add-next'),
          handler: this.queue_add_next,
          icon: 'playlist-play'
        },
        {
          label: this.$t('dialog.directory.play'),
          handler: this.play,
          icon: 'play'
        }
      ]
    }
  },
  methods: {
    play() {
      this.$emit('close')
      webapi.player_play_expression(
        `path starts with "${this.item}" order by path asc`,
        false
      )
    },
    queue_add() {
      this.$emit('close')
      webapi.queue_expression_add(
        `path starts with "${this.item}" order by path asc`
      )
    },
    queue_add_next() {
      this.$emit('close')
      webapi.queue_expression_add_next(
        `path starts with "${this.item}" order by path asc`
      )
    }
  }
}
</script>
