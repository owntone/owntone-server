<template>
  <div>
    <transition name="fade">
      <div v-if="show" class="modal is-active">
        <div class="modal-background" @click="$emit('close')" />
        <div class="modal-content fd-modal-card">
          <div class="card">
            <div class="card-content">
              <p class="title is-4">
                {{ track.title }}
              </p>
              <p class="subtitle">
                {{ track.artist }}
              </p>
              <div v-if="track.media_kind === 'podcast'" class="buttons">
                <a
                  v-if="track.play_count > 0"
                  class="button is-small"
                  @click="mark_new"
                  >Mark as new</a
                >
                <a
                  v-if="track.play_count === 0"
                  class="button is-small"
                  @click="mark_played"
                  >Mark as played</a
                >
              </div>
              <div class="content is-small">
                <p>
                  <span class="heading">Album</span>
                  <a class="title is-6 has-text-link" @click="open_album">{{
                    track.album
                  }}</a>
                </p>
                <p
                  v-if="track.album_artist && track.media_kind !== 'audiobook'"
                >
                  <span class="heading">Album artist</span>
                  <a class="title is-6 has-text-link" @click="open_artist">{{
                    track.album_artist
                  }}</a>
                </p>
                <p v-if="track.composer">
                  <span class="heading">Composer</span>
                  <span class="title is-6">{{ track.composer }}</span>
                </p>
                <p v-if="track.date_released">
                  <span class="heading">Release date</span>
                  <span class="title is-6">{{
                    $filters.time(track.date_released, 'L')
                  }}</span>
                </p>
                <p v-else-if="track.year > 0">
                  <span class="heading">Year</span>
                  <span class="title is-6">{{ track.year }}</span>
                </p>
                <p v-if="track.genre">
                  <span class="heading">Genre</span>
                  <a class="title is-6 has-text-link" @click="open_genre">{{
                    track.genre
                  }}</a>
                </p>
                <p>
                  <span class="heading">Track / Disc</span>
                  <span class="title is-6"
                    >{{ track.track_number }} / {{ track.disc_number }}</span
                  >
                </p>
                <p>
                  <span class="heading">Length</span>
                  <span class="title is-6">{{
                    $filters.duration(track.length_ms)
                  }}</span>
                </p>
                <p>
                  <span class="heading">Path</span>
                  <span class="title is-6">{{ track.path }}</span>
                </p>
                <p>
                  <span class="heading">Type</span>
                  <span class="title is-6"
                    >{{ track.media_kind }} - {{ track.data_kind }}
                    <span
                      v-if="track.data_kind === 'spotify'"
                      class="has-text-weight-normal"
                      >(<a @click="open_spotify_artist">artist</a>,
                      <a @click="open_spotify_album">album</a>)</span
                    ></span
                  >
                </p>
                <p>
                  <span class="heading">Quality</span>
                  <span class="title is-6">
                    {{ track.type }}
                    <span v-if="track.samplerate">
                      | {{ track.samplerate }} Hz</span
                    >
                    <span v-if="track.channels">
                      | {{ $filters.channels(track.channels) }}</span
                    >
                    <span v-if="track.bitrate">
                      | {{ track.bitrate }} Kb/s</span
                    >
                  </span>
                </p>
                <p>
                  <span class="heading">Added at</span>
                  <span class="title is-6">{{
                    $filters.time(track.time_added, 'L LT')
                  }}</span>
                </p>
                <p>
                  <span class="heading">Rating</span>
                  <span class="title is-6"
                    >{{ Math.floor(track.rating / 10) }} / 10</span
                  >
                </p>
                <p v-if="track.comment">
                  <span class="heading">Comment</span>
                  <span class="title is-6">{{ track.comment }}</span>
                </p>
              </div>
            </div>
            <footer class="card-footer">
              <a class="card-footer-item has-text-dark" @click="queue_add">
                <span class="icon"><i class="mdi mdi-playlist-plus" /></span>
                <span class="is-size-7">Add</span>
              </a>
              <a class="card-footer-item has-text-dark" @click="queue_add_next">
                <span class="icon"><i class="mdi mdi-playlist-play" /></span>
                <span class="is-size-7">Add Next</span>
              </a>
              <a class="card-footer-item has-text-dark" @click="play_track">
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
import SpotifyWebApi from 'spotify-web-api-js'

export default {
  name: 'ModalDialogTrack',

  props: ['show', 'track'],
  emits: ['close', 'play-count-changed'],

  data() {
    return {
      spotify_track: {}
    }
  },

  watch: {
    track() {
      if (this.track && this.track.data_kind === 'spotify') {
        const spotifyApi = new SpotifyWebApi()
        spotifyApi.setAccessToken(this.$store.state.spotify.webapi_token)
        spotifyApi
          .getTrack(this.track.path.slice(this.track.path.lastIndexOf(':') + 1))
          .then((response) => {
            this.spotify_track = response
          })
      } else {
        this.spotify_track = {}
      }
    }
  },

  methods: {
    play_track: function () {
      this.$emit('close')
      webapi.player_play_uri(this.track.uri, false)
    },

    queue_add: function () {
      this.$emit('close')
      webapi.queue_add(this.track.uri)
    },

    queue_add_next: function () {
      this.$emit('close')
      webapi.queue_add_next(this.track.uri)
    },

    open_album: function () {
      this.$emit('close')
      if (this.track.media_kind === 'podcast') {
        this.$router.push({ path: '/podcasts/' + this.track.album_id })
      } else if (this.track.media_kind === 'audiobook') {
        this.$router.push({ path: '/audiobooks/' + this.track.album_id })
      } else {
        this.$router.push({ path: '/music/albums/' + this.track.album_id })
      }
    },

    open_artist: function () {
      this.$emit('close')
      this.$router.push({
        path: '/music/artists/' + this.track.album_artist_id
      })
    },

    open_genre: function () {
      this.$router.push({ name: 'Genre', params: { genre: this.track.genre } })
    },

    open_spotify_artist: function () {
      this.$emit('close')
      this.$router.push({
        path: '/music/spotify/artists/' + this.spotify_track.artists[0].id
      })
    },

    open_spotify_album: function () {
      this.$emit('close')
      this.$router.push({
        path: '/music/spotify/albums/' + this.spotify_track.album.id
      })
    },

    mark_new: function () {
      webapi
        .library_track_update(this.track.id, { play_count: 'reset' })
        .then(() => {
          this.$emit('play-count-changed')
          this.$emit('close')
        })
    },

    mark_played: function () {
      webapi
        .library_track_update(this.track.id, { play_count: 'increment' })
        .then(() => {
          this.$emit('play-count-changed')
          this.$emit('close')
        })
    }
  }
}
</script>

<style></style>
