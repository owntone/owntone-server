<template>
  <modal-dialog :actions="actions" :show="show" @close="$emit('close')">
    <template #content>
      <list-properties :item="item">
        <template #buttons>
          <div class="buttons">
            <a
              v-for="button in buttons"
              :key="button.label"
              v-t="button.label"
              class="button is-small"
              @click="button.handler"
            />
          </div>
        </template>
      </list-properties>
    </template>
  </modal-dialog>
</template>

<script>
import ListProperties from '@/components/ListProperties.vue'
import ModalDialog from '@/components/ModalDialog.vue'
import webapi from '@/webapi'

export default {
  name: 'ModalDialogPlayable',
  components: { ListProperties, ModalDialog },
  props: {
    buttons: { default: null, type: Array },
    item: { required: true, type: Object },
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
      if (this.item.expression) {
        webapi.player_play_expression(this.item.expression, false)
      } else {
        webapi.player_play_uri(this.item.uris || this.item.item.uri, false)
      }
    },
    queue_add() {
      this.$emit('close')
      if (this.item.expression) {
        webapi.queue_expression_add(this.item.expression)
      } else {
        webapi.queue_add(this.item.uris || this.item.uri)
      }
    },
    queue_add_next() {
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
