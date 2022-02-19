<template>
  <div>
    <transition name="fade">
      <div v-if="show" class="modal is-active">
        <div class="modal-background" @click="$emit('close')" />
        <div class="modal-content fd-modal-card">
          <div class="card">
            <div class="card-content">
              <p class="title is-4">
                {{ track.name }}
              </p>
              <p class="subtitle">
                {{ track.artists[0].name }}
              </p>
              <div class="content is-small">
                <p>
                  <span class="heading">Album</span>
                  <a class="title is-6 has-text-link" @click="open_album">{{
                    album.name
                  }}</a>
                </p>
                <p>
                  <span class="heading">Album artist</span>
                  <a class="title is-6 has-text-link" @click="open_artist">{{
                    album.artists[0].name
                  }}</a>
                </p>
                <p>
                  <span class="heading">Release date</span>
                  <span class="title is-6">{{
                    $filters.time(album.release_date, 'L')
                  }}</span>
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
                    $filters.duration(track.duration_ms)
                  }}</span>
                </p>
                <p>
                  <span class="heading">Path</span>
                  <span class="title is-6">{{ track.uri }}</span>
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
  name: 'SpotifyModalDialogTrack',
  props: ['show', 'track', 'album'],

  methods: {
    play: function () {
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
      this.$router.push({ path: '/music/spotify/albums/' + this.album.id })
    },

    open_artist: function () {
      this.$router.push({
        path: '/music/spotify/artists/' + this.album.artists[0].id
      })
    }
  }
}
</script>

<style></style>
