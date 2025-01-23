<template>
  <transition name="fade">
    <div v-if="show" class="modal is-active">
      <div class="modal-background" @click="$emit('close')" />
      <div class="modal-content">
        <div class="card">
          <div class="card-content">
            <p class="title is-4">
              <a class="has-text-link" @click="open" v-text="item.name" />
            </p>
            <div class="content is-small">
              <p>
                <span class="heading" v-text="$t('dialog.playlist.path')" />
                <span class="title is-6" v-text="item.path" />
              </p>
              <p>
                <span class="heading" v-text="$t('dialog.playlist.type')" />
                <span
                  class="title is-6"
                  v-text="$t(`playlist.type.${item.type}`)"
                />
              </p>
              <p v-if="!item.folder">
                <span class="heading" v-text="$t('dialog.playlist.tracks')" />
                <span class="title is-6" v-text="item.item_count" />
              </p>
            </div>
          </div>
          <footer v-if="!item.folder" class="card-footer">
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
          </footer>
        </div>
      </div>
      <button
        class="modal-close is-large"
        aria-label="close"
        @click="$emit('close')"
      />
    </div>
  </transition>
</template>

<script>
import webapi from '@/webapi'

export default {
  name: 'ModalDialogPlaylist',
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

<style></style>
