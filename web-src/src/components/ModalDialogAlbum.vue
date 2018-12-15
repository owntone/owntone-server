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
                <p v-if="album.artist && media_kind !== 'audiobook'">
                  <span class="heading">Album artist</span>
                  <a class="title is-6 has-text-link" @click="open_artist">{{ album.artist }}</a>
                </p>
                <p v-if="album.artist && media_kind === 'audiobook'">
                  <span class="heading">Album artist</span>
                  <span class="title is-6">{{ album.artist }}</span>
                </p>
                <p>
                  <span class="heading">Tracks</span>
                  <span class="title is-6">{{ album.track_count }}</span>
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
  name: 'ModalDialogAlbum',
  props: [ 'show', 'album', 'media_kind' ],

  data () {
    return {
      artwork_visible: false
    }
  },

  computed: {
    artwork_url: function () {
      if (this.album.artwork_url && this.album.artwork_url.startsWith('/')) {
        return this.album.artwork_url + '?maxwidth=600&maxheight=600'
      }
      return this.album.artwork_url
    }
  },

  methods: {
    play: function () {
      this.$emit('close')
      webapi.player_play_uri(this.album.uri, false)
    },

    queue_add: function () {
      this.$emit('close')
      webapi.queue_add(this.album.uri).then(() =>
        this.$store.dispatch('add_notification', { text: 'Album tracks appended to queue', type: 'info', timeout: 2000 })
      )
    },

    queue_add_next: function () {
      this.$emit('close')
      webapi.queue_add_next(this.album.uri).then(() =>
        this.$store.dispatch('add_notification', { text: 'Album tracks appended to queue', type: 'info', timeout: 2000 })
      )
    },

    open_album: function () {
      this.show_details_modal = false
      if (this.media_kind === 'podcast') {
        this.$router.push({ path: '/podcasts/' + this.album.id })
      } else if (this.media_kind === 'audiobook') {
        this.$router.push({ path: '/audiobooks/' + this.album.id })
      } else {
        this.$router.push({ path: '/music/albums/' + this.album.id })
      }
    },

    open_artist: function () {
      this.show_details_modal = false
      this.$router.push({ path: '/music/artists/' + this.album.artist_id })
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
