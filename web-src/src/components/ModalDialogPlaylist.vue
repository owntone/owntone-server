<template>
  <div>
    <transition name="fade">
      <div v-if="show" class="modal is-active">
        <div class="modal-background" @click="$emit('close')" />
        <div class="modal-content fd-modal-card">
          <div class="card">
            <div class="card-content">
              <p class="title is-4">
                <a class="has-text-link" @click="open_playlist">{{
                  playlist.name
                }}</a>
              </p>
              <div class="content is-small">
                <p>
                  <span class="heading">Path</span>
                  <span class="title is-6">{{ playlist.path }}</span>
                </p>
                <p>
                  <span class="heading">Type</span>
                  <span class="title is-6">{{ playlist.type }}</span>
                </p>
              </div>
            </div>
            <footer v-if="!playlist.folder" class="card-footer">
              <a class="card-footer-item has-text-dark" @click="queue_add">
                <span class="icon"><i class="mdi mdi-playlist-plus" /></span>
                <span class="is-size-7">Add</span>
              </a>
              <a class="card-footer-item has-text-dark" @click="queue_add_next">
                <span class="icon"><i class="mdi mdi-playlist-play" /></span>
                <span class="is-size-7">Add Next</span>
              </a>
              <a class="card-footer-item has-text-dark" @click="play">
                <span class="icon"><i class="mdi mdi-play" /></span>
                <span class="is-size-7">Play</span>
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
  </div>
</template>

<script>
import webapi from '@/webapi'

export default {
  name: 'ModalDialogPlaylist',
  props: ['show', 'playlist', 'uris'],

  methods: {
    play: function () {
      this.$emit('close')
      webapi.player_play_uri(this.uris ? this.uris : this.playlist.uri, false)
    },

    queue_add: function () {
      this.$emit('close')
      webapi.queue_add(this.uris ? this.uris : this.playlist.uri)
    },

    queue_add_next: function () {
      this.$emit('close')
      webapi.queue_add_next(this.uris ? this.uris : this.playlist.uri)
    },

    open_playlist: function () {
      this.$emit('close')
      this.$router.push({ path: '/playlists/' + this.playlist.id + '/tracks' })
    }
  }
}
</script>

<style></style>
