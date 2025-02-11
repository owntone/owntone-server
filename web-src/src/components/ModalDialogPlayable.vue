<template>
  <modal-dialog :actions="actions" :show="show" @close="$emit('close')">
    <template #content>
      <div class="title is-4">
        <a v-if="item.action" @click="item.action" v-text="item.name"></a>
        <span v-else v-text="item.name" />
      </div>
      <cover-artwork
        v-if="item.image"
        :url="item.image"
        :artist="item.artist"
        :album="item.name"
        class="is-normal mb-5"
      />
      <div class="buttons">
        <a
          v-for="button in buttons"
          :key="button.label"
          v-t="button.label"
          class="button is-small"
          @click="button.action"
        />
      </div>
      <div
        v-for="property in item.properties?.filter((p) => p.value)"
        :key="property.label"
        class="mb-3"
      >
        <div v-t="property.label" class="is-size-7 is-uppercase" />
        <div class="title is-6">
          <a
            v-if="property.action"
            @click="property.action"
            v-text="property.value"
          />
          <span v-else class="title is-6" v-text="property.value" />
        </div>
      </div>
    </template>
  </modal-dialog>
</template>

<script>
import CoverArtwork from '@/components/CoverArtwork.vue'
import ModalDialog from '@/components/ModalDialog.vue'
import webapi from '@/webapi'

export default {
  name: 'ModalDialogPlayable',
  components: { ModalDialog, CoverArtwork },
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
