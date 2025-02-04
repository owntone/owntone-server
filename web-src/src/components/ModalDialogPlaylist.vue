<template>
  <modal-dialog :show="show" @close="$emit('close')">
    <template #content>
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
    <template v-if="!item.folder" #footer>
      <a class="card-footer-item has-text-dark" @click="queue_add">
        <mdicon class="icon" name="playlist-plus" size="16" />
        <span class="is-size-7" v-text="$t('dialog.playlist.add')" />
      </a>
      <a class="card-footer-item has-text-dark" @click="queue_add_next">
        <mdicon class="icon" name="playlist-play" size="16" />
        <span class="is-size-7" v-text="$t('dialog.playlist.add-next')" />
      </a>
      <a class="card-footer-item has-text-dark" @click="play">
        <mdicon class="icon" name="play" size="16" />
        <span class="is-size-7" v-text="$t('dialog.playlist.play')" />
      </a>
    </template>
  </modal-dialog>
</template>

<script>
import ModalDialog from '@/components/ModalDialog.vue'
import webapi from '@/webapi'

export default {
  name: 'ModalDialogPlaylist',
  components: { ModalDialog },
  props: {
    item: { required: true, type: Object },
    show: Boolean,
    uris: { default: '', type: String }
  },
  emits: ['close'],

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
