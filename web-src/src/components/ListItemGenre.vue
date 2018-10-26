<template>
  <div class="media">
    <div class="media-content fd-has-action is-clipped" v-on:click="open_genre">
      <h1 class="title is-6">{{ genre.name }}</h1>
    </div>
    <div class="media-right">
      <a @click="show_details_modal = true">
        <span class="icon has-text-dark"><i class="mdi mdi-dots-vertical mdi-18px"></i></span>
      </a>
      <modal-dialog :show="show_details_modal" @close="show_details_modal = false">
        <template slot="modal-content">
          <div class="card">
            <div class="card-content">
              <p class="title is-4">
                <a class="has-text-link" @click="open_genre">{{ genre.name }}</a>
              </p>
<!--
              <div class="content is-small">
                <p>
                  <span class="heading">Albums</span>
                  <span class="title is-6">{{ genre.album_count }}</span>
                </p>
                <p>
                  <span class="heading">Tracks</span>
                  <span class="title is-6">{{ genre.track_count }}</span>
                </p>
              </div>
 -->
            </div>
            <footer class="card-footer">
              <a class="card-footer-item has-text-dark" @click="queue_add">
                <span class="icon"><i class="mdi mdi-playlist-plus mdi-12px"></i></span> <span>Add</span>
              </a>
              <a class="card-footer-item has-text-dark" @click="queue_add_next">
                <span class="icon"><i class="mdi mdi-playlist-play mdi-12px"></i></span> <span>Play Next</span>
              </a>
              <a class="card-footer-item has-text-dark" @click="play">
                <span class="icon"><i class="mdi mdi-play mdi-12px"></i></span> <span>Play</span>
              </a>
            </footer>
          </div>
        </template>
      </modal-dialog>
    </div>
  </div>
</template>

<script>
import ModalDialog from '@/components/ModalDialog'
import webapi from '@/webapi'

export default {
  name: 'PartGenre',
  components: { ModalDialog },

  props: [ 'genre' ],

  data () {
    return {
      show_details_modal: false
    }
  },

  methods: {
    play: function () {
      this.show_details_modal = false
      webapi.queue_clear().then(() =>
        webapi.queue_add(webapi.library_genre(this.genre.name).then(function (response) {
          webapi.queue_add(response.data.albums.items.map(a => a.uri).join(',')).then(() =>
            webapi.player_play()
          )
        }))
      )
    },

    queue_add: function () {
      this.show_details_modal = false
      webapi.library_genre(this.genre.name).then(function (response) {
        webapi.queue_add(response.data.albums.items.map(a => a.uri).join(','))
      })
      this.$store.dispatch('add_notification', { text: 'Genre albums appended to queue', type: 'info', timeout: 1500 })
    },

    queue_add_next: function () {
      this.show_details_modal = false
      webapi.library_genre(this.genre.name).then(function (response) {
        webapi.queue_add_next(response.data.albums.items.map(a => a.uri).join(','))
      })
      this.$store.dispatch('add_notification', { text: 'Genre albums playing next', type: 'info', timeout: 1500 })
    },

    open_genre: function () {
      this.show_details_modal = false
      this.$router.push({ name: 'Genre', params: { genre: this.genre.name } })
    }
  }
}
</script>

<style>
</style>
