<template>
  <div>
    <transition name="fade">
      <div class="modal is-active" v-if="show">
        <div class="modal-background" @click="$emit('close')"></div>
        <div class="modal-content fd-modal-card">
          <div class="card">
            <div class="card-content">
              <p class="title is-4">
                <a class="has-text-link" @click="open_albums">{{ genre.name }}</a>
              </p>
              <p>
                <span class="heading">Albums</span>
                <a class="has-text-link is-6" @click="open_albums">{{ genre.album_count }}</a>
              </p>
              <p>
                <span class="heading">Artists</span>
                <a class="has-text-link is-6" @click="open_artists">{{ genre.artist_count }}</a>
              </p>
              <p>
                <span class="heading">Tracks</span>
                <a class="has-text-link is-6" @click="open_tracks">{{ genre.track_count }}</a>
              </p>
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
  name: 'ModalDialogGenre',
  props: [ 'show', 'genre' ],

  methods: {
    play: function () {
      this.$emit('close')
      webapi.player_play_expression('genre is "' + this.genre.name + '" and media_kind is music', false)
    },

    queue_add: function () {
      this.$emit('close')
      webapi.queue_expression_add('genre is "' + this.genre.name + '" and media_kind is music')
    },

    queue_add_next: function () {
      this.$emit('close')
      webapi.queue_expression_add_next('genre is "' + this.genre.name + '" and media_kind is music')
    },

    open_albums: function () {
      this.$emit('close')
      this.$router.push({ name: 'Genre', params: { genre: this.genre.name } })
    },

    open_tracks: function () {
      this.show_details_modal = false
      this.$router.push({ name: 'GenreTracks', params: { genre: this.genre.name } })
    },

    open_artists: function () {
      this.show_details_modal = false
      this.$router.push({ name: 'GenreArtists', params: { genre: this.genre.name } })
    }
  }
}
</script>

<style>
</style>
