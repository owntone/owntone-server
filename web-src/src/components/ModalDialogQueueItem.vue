<template>
  <div>
    <transition name="fade">
      <div v-if="show" class="modal is-active">
        <div class="modal-background" @click="$emit('close')" />
        <div class="modal-content fd-modal-card">
          <div class="card">
            <div class="card-content">
              <p class="title is-4">
                {{ item.title }}
              </p>
              <p class="subtitle">
                {{ item.artist }}
              </p>
              <div class="content is-small">
                <p>
                  <span class="heading">Album</span>
                  <a
                    v-if="item.album_id"
                    class="title is-6 has-text-link"
                    @click="open_album"
                    >{{ item.album }}</a
                  >
                  <span v-else class="title is-6">{{ item.album }}</span>
                </p>
                <p v-if="item.album_artist">
                  <span class="heading">Album artist</span>
                  <a
                    v-if="item.album_artist_id"
                    class="title is-6 has-text-link"
                    @click="open_album_artist"
                    >{{ item.album_artist }}</a
                  >
                  <span v-else class="title is-6">{{ item.album_artist }}</span>
                </p>
                <p v-if="item.composer">
                  <span class="heading">Composer</span>
                  <span class="title is-6">{{ item.composer }}</span>
                </p>
                <p v-if="item.year > 0">
                  <span class="heading">Year</span>
                  <span class="title is-6">{{ item.year }}</span>
                </p>
                <p v-if="item.genre">
                  <span class="heading">Genre</span>
                  <a class="title is-6 has-text-link" @click="open_genre">{{
                    item.genre
                  }}</a>
                </p>
                <p>
                  <span class="heading">Track / Disc</span>
                  <span class="title is-6"
                    >{{ item.track_number }} / {{ item.disc_number }}</span
                  >
                </p>
                <p>
                  <span class="heading">Length</span>
                  <span class="title is-6">{{
                    $filters.duration(item.length_ms)
                  }}</span>
                </p>
                <p>
                  <span class="heading">Path</span>
                  <span class="title is-6">{{ item.path }}</span>
                </p>
                <p>
                  <span class="heading">Type</span>
                  <span class="title is-6"
                    >{{ item.media_kind }} - {{ item.data_kind }}
                    <span
                      v-if="item.data_kind === 'spotify'"
                      class="has-text-weight-normal"
                      >(<a @click="open_spotify_artist">artist</a>,
                      <a @click="open_spotify_album">album</a>)</span
                    ></span
                  >
                </p>
                <p>
                  <span class="heading">Quality</span>
                  <span class="title is-6">
                    {{ item.type }}
                    <span v-if="item.samplerate">
                      | {{ item.samplerate }} Hz</span
                    >
                    <span v-if="item.channels">
                      | {{ $filters.channels(item.channels) }}</span
                    >
                    <span v-if="item.bitrate"> | {{ item.bitrate }} Kb/s</span>
                  </span>
                </p>
              </div>
            </div>
            <footer class="card-footer">
              <a class="card-footer-item has-text-dark" @click="remove">
                <span class="icon"><i class="mdi mdi-delete" /></span>
                <span class="is-size-7">Remove</span>
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
import SpotifyWebApi from 'spotify-web-api-js'

export default {
  name: 'ModalDialogQueueItem',
  props: ['show', 'item'],

  data() {
    return {
      spotify_track: {}
    }
  },

  watch: {
    item() {
      if (this.item && this.item.data_kind === 'spotify') {
        const spotifyApi = new SpotifyWebApi()
        spotifyApi.setAccessToken(this.$store.state.spotify.webapi_token)
        spotifyApi
          .getTrack(this.item.path.slice(this.item.path.lastIndexOf(':') + 1))
          .then((response) => {
            this.spotify_track = response
          })
      } else {
        this.spotify_track = {}
      }
    }
  },

  methods: {
    remove: function () {
      this.$emit('close')
      webapi.queue_remove(this.item.id)
    },

    play: function () {
      this.$emit('close')
      webapi.player_play({ item_id: this.item.id })
    },

    open_album: function () {
      if (this.media_kind === 'podcast') {
        this.$router.push({ path: '/podcasts/' + this.item.album_id })
      } else if (this.media_kind === 'audiobook') {
        this.$router.push({ path: '/audiobooks/' + this.item.album_id })
      } else {
        this.$router.push({ path: '/music/albums/' + this.item.album_id })
      }
    },

    open_album_artist: function () {
      this.$router.push({ path: '/music/artists/' + this.item.album_artist_id })
    },

    open_genre: function () {
      this.$router.push({ name: 'Genre', params: { genre: this.item.genre } })
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
    }
  }
}
</script>

<style></style>
