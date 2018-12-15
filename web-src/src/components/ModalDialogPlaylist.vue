<template>
  <div>
    <transition name="fade">
      <div class="modal is-active" v-if="show">
        <div class="modal-background" @click="$emit('close')"></div>
        <div class="modal-content fd-modal-card">
          <div class="card">
            <div class="card-content">
              <p class="title is-4">
                <a class="has-text-link" @click="open_playlist">{{ playlist.name }}</a>
              </p>
              <div class="content is-small">
                <p>
                  <span class="heading">Path</span>
                  <span class="title is-6">{{ playlist.path }}</span>
                </p>
              </div>
            </div>
            <footer class="card-footer">
              <a class="card-footer-item has-text-dark" @click="queue_add">
                <span class="icon"><i class="mdi mdi-playlist-plus mdi-18px"></i></span> <span>Add</span>
              </a>
              <a class="card-footer-item has-text-dark" @click="queue_add_next">
                <span class="icon"><i class="mdi mdi-playlist-play mdi-18px"></i></span> <span>Add Next</span>
              </a>
              <a class="card-footer-item has-text-dark" @click="play">
                <span class="icon"><i class="mdi mdi-play mdi-18px"></i></span> <span>Play</span>
              </a>
            </footer>
          </div>
        </div>
        <button class="modal-close is-large" aria-label="close" @click="$emit('close')"></button>
      </div>
    </transition>
  </div>
</template>

<script>
import webapi from '@/webapi'

export default {
  name: 'ModalDialogPlaylist',
  props: [ 'show', 'playlist' ],

  methods: {
    play: function () {
      this.$emit('close')
      webapi.player_play_uri(this.playlist.uri, false)
    },

    queue_add: function () {
      this.$emit('close')
      webapi.queue_add(this.playlist.uri).then(() =>
        this.$store.dispatch('add_notification', { text: 'Playlist appended to queue', type: 'info', timeout: 2000 })
      )
    },

    queue_add_next: function () {
      this.$emit('close')
      webapi.queue_add_next(this.playlist.uri).then(() =>
        this.$store.dispatch('add_notification', { text: 'Album tracks appended to queue', type: 'info', timeout: 2000 })
      )
    },

    open_playlist: function () {
      this.$emit('close')
      this.$router.push({ path: '/playlists/' + this.playlist.id })
    }
  }
}
</script>

<style>
</style>
