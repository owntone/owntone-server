<template>
  <div class="media">
    <div class="media-content fd-has-action is-clipped" v-on:click="open_album">
      <h1 class="title is-6">{{ album.name }}</h1>
      <h2 class="subtitle is-7 has-text-grey"><b>{{ album.artists[0].name }}</b></h2>
    </div>
    <div class="media-right">
      <a @click="show_details">
        <span class="icon has-text-dark"><i class="mdi mdi-dots-vertical mdi-18px"></i></span>
      </a>
      <transition name="fade">
        <div class="modal is-active" v-if="show_details_modal">
          <div class="modal-background" @click="hide_details"></div>
          <div class="modal-content">
            <div class="card">
              <div class="card-content">
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
                    <span class="title is-6">{{ album.release_date }}</span>
                  </p>
                  <p>
                    <span class="heading">Type</span>
                    <span class="title is-6">{{ album.album_type }}</span>
                  </p>
                </div>
              </div>
              <footer class="card-footer">
                <a class="card-footer-item has-text-dark" @click="queue_add">
                  <span class="icon"><i class="mdi mdi-playlist-plus mdi-18px"></i></span> <span>Add</span>
                </a>
                <a class="card-footer-item has-text-dark" @click="play">
                  <span class="icon"><i class="mdi mdi-play mdi-18px"></i></span> <span>Play</span>
                </a>
              </footer>
            </div>
          </div>
          <button class="modal-close is-large" aria-label="close" @click="hide_details"></button>
        </div>
      </transition>
    </div>
  </div>
</template>

<script>
import webapi from '@/webapi'

export default {
  name: 'SpotifyListItemAlbum',

  props: ['album'],

  data () {
    return {
      show_details_modal: false
    }
  },

  methods: {
    play: function () {
      webapi.queue_clear().then(() =>
        webapi.queue_add(this.album.uri).then(() =>
          webapi.player_play()
        )
      )
      this.show_details_modal = false
    },

    queue_add: function () {
      webapi.queue_add(this.album.uri).then(
        // this.$store.commit(types.ADD_NOTIFICATION, { text: 'Album tracks appended to queue', timeout: 0 })
        this.$store.dispatch('add_notification', { text: 'Album tracks appended to queue', type: 'info', timeout: 3000 })
      )
      this.show_details_modal = false
    },

    show_details: function () {
      this.show_details_modal = true
    },

    hide_details: function () {
      this.show_details_modal = false
    },

    open_album: function () {
      this.$router.push({ path: '/music/spotify/albums/' + this.album.id })
    },

    open_artist: function () {
      this.show_details_modal = false
      this.$router.push({ path: '/music/spotify/artists/' + this.album.artists[0].id })
    }
  }
}
</script>

<style>
</style>
