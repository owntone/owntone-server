<template>
  <modal-dialog-action
    :actions="actions"
    :show="show"
    @add="queue_add"
    @add-next="queue_add_next"
    @close="$emit('close')"
    @play="play"
  >
    <template #modal-content>
      <div class="title is-4">
        <a @click="open" v-text="item.name" />
      </div>
      <div class="mb-3">
        <div
          class="is-size-7 is-uppercase"
          v-text="$t('dialog.playlist.path')"
        />
        <div class="title is-6" v-text="item.path" />
      </div>
      <div class="mb-3">
        <div
          class="is-size-7 is-uppercase"
          v-text="$t('dialog.playlist.type')"
        />
        <div class="title is-6" v-text="$t(`playlist.type.${item.type}`)" />
      </div>
      <div v-if="!item.folder" class="mb-3">
        <div
          class="is-size-7 is-uppercase"
          v-text="$t('dialog.playlist.tracks')"
        />
        <div class="title is-6" v-text="item.item_count" />
      </div>
    </template>
  </modal-dialog-action>
</template>

<script>
import ModalDialogAction from '@/components/ModalDialogAction.vue'
import webapi from '@/webapi'

export default {
  name: 'ModalDialogPlaylist',
  components: { ModalDialogAction },
  props: {
    item: { required: true, type: Object },
    show: Boolean,
    uris: { default: '', type: String }
  },
  emits: ['close'],
  computed: {
    actions() {
      if (!this.item.folder) {
        return [
          {
            label: this.$t('dialog.playlist.add'),
            event: 'add',
            icon: 'playlist-plus'
          },
          {
            label: this.$t('dialog.playlist.add-next'),
            event: 'add-next',
            icon: 'playlist-play'
          },
          {
            label: this.$t('dialog.playlist.play'),
            event: 'play',
            icon: 'play'
          }
        ]
      }
      return []
    }
  },
  methods: {
    open() {
      this.$emit('close')
      this.$router.push({
        name: 'playlist',
        params: { id: this.item.id }
      })
    },
    play() {
      this.$emit('close')
      webapi.player_play_uri(this.uris || this.item.uri, false)
    },
    queue_add() {
      this.$emit('close')
      webapi.queue_add(this.uris || this.item.uri)
    },
    queue_add_next() {
      this.$emit('close')
      webapi.queue_add_next(this.uris || this.item.uri)
    }
  }
}
</script>
