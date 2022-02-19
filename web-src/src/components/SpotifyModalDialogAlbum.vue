<template>
  <div>
    <transition name="fade">
      <div class="modal is-active" v-if="show">
        <div class="modal-background" @click="$emit('close')"></div>
        <div class="modal-content fd-modal-card">
          <div class="card">
            <div class="card-content">
              <figure class="image is-square fd-has-margin-bottom" v-show="artwork_visible">
                <img :src="artwork_url" @load="artwork_loaded" @error="artwork_error" class="fd-has-shadow">
              </figure>
              <p class="title is-4">
                <a class="has-text-link" @click="open_album">{{ album.name }}</a>
              </p>
              <div class="content is-small">
                <p>
                  <span class="heading">Album artist</span>
                  <a class="title is-6 has-text-link" @click="open_artist">{{ album.artists[0].name }}</a>
                </p>
                <p>
                  <span class="heading">Release date</span>
                  <span class="title is-6">{{ $filters.time(album.release_date, 'L') }}</span>
                </p>
                <p>
                  <span class="heading">Type</span>
                  <span class="title is-6">{{ album.album_type }}</span>
                </p>
              </div>
            </div>
            <footer class="card-footer">
              <a class="card-footer-item has-text-dark" @click="queue_add">
                <span class="icon"><i class="mdi mdi-playlist-plus"></i></span> <span class="is-size-7">Add</span>
              </a>
              <a class="card-footer-item has-text-dark" @click="queue_add_next">
                <span class="icon"><i class="mdi mdi-playlist-play"></i></span> <span class="is-size-7">Add Next</span>
              </a>
              <a class="card-footer-item has-text-dark" @click="play">
                <span class="icon"><i class="mdi mdi-play"></i></span> <span class="is-size-7">Play</span>
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
  name: 'SpotifyModalDialogAlbum',
  props: ['show', 'album'],

  data () {
    return {
      artwork_visible: false
    }
  },

  computed: {
    artwork_url: function () {
      if (this.album.images && this.album.images.length > 0) {
        return this.album.images[0].url
      }
      return ''
    }
  },

  methods: {
    play: function () {
      this.$emit('close')
      webapi.player_play_uri(this.album.uri, false)
    },

    queue_add: function () {
      this.$emit('close')
      webapi.queue_add(this.album.uri)
    },

    queue_add_next: function () {
      this.$emit('close')
      webapi.queue_add_next(this.album.uri)
    },

    open_album: function () {
      this.$router.push({ path: '/music/spotify/albums/' + this.album.id })
    },

    open_artist: function () {
      this.$router.push({ path: '/music/spotify/artists/' + this.album.artists[0].id })
    },

    artwork_loaded: function () {
      this.artwork_visible = true
    },

    artwork_error: function () {
      this.artwork_visible = false
    }
  }
}
</script>

<style>
</style>
