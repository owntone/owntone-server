<template>
  <div>
    <transition name="fade">
      <div class="modal is-active" v-if="show">
        <div class="modal-background" @click="$emit('close')"></div>
        <div class="modal-content fd-modal-card">
          <div class="card">
            <div class="card-content">
              <p class="title is-4">
                {{ track.title }}
              </p>
              <p class="subtitle">
                {{ track.artist }}
              </p>
              <div class="content is-small">
                <p>
                  <span class="heading">Album</span>
                  <a class="title is-6 has-text-link" @click="open_album">{{ track.album }}</a>
                </p>
                <p v-if="track.album_artist && track.media_kind !== 'audiobook'">
                  <span class="heading">Album artist</span>
                  <a class="title is-6 has-text-link" @click="open_artist">{{ track.album_artist }}</a>
                </p>
                <p v-if="track.composer">
                  <span class="heading">Composer</span>
                  <span class="title is-6">{{ track.composer }}</span>
                </p>
                <p v-if="track.date_released">
                  <span class="heading">Release date</span>
                  <span class="title is-6">{{ track.date_released | time('L')}}</span>
                </p>
                <p v-else-if="track.year > 0">
                  <span class="heading">Year</span>
                  <span class="title is-6">{{ track.year }}</span>
                </p>
                <p>
                  <span class="heading">Genre</span>
                  <span class="title is-6">{{ track.genre }}</span>
                </p>
                <p>
                  <span class="heading">Track / Disc</span>
                  <span class="title is-6">{{ track.track_number }} / {{ track.disc_number }}</span>
                </p>
                <p>
                  <span class="heading">Length</span>
                  <span class="title is-6">{{ track.length_ms | duration }}</span>
                </p>
                <p>
                  <span class="heading">Path</span>
                  <span class="title is-6">{{ track.path }}</span>
                </p>
                <p>
                  <span class="heading">Type</span>
                  <span class="title is-6">{{ track.media_kind }} - {{ track.data_kind }}</span>
                </p>
                <p>
                  <span class="heading">Added at</span>
                  <span class="title is-6">{{ track.time_added | time('L LT')}}</span>
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
              <a class="card-footer-item has-text-dark" @click="play_track">
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
  name: 'ModalDialogTrack',

  props: ['show', 'track'],

  data () {
    return {
    }
  },

  methods: {
    play_track: function () {
      this.$emit('close')
      webapi.player_play_uri(this.track.uri, false)
    },

    queue_add: function () {
      this.$emit('close')
      webapi.queue_add(this.track.uri).then(() =>
        this.$store.dispatch('add_notification', { text: 'Track appended to queue', type: 'info', timeout: 2000 })
      )
    },

    queue_add_next: function () {
      this.$emit('close')
      webapi.queue_add_next(this.track.uri).then(() =>
        this.$store.dispatch('add_notification', { text: 'Album tracks appended to queue', type: 'info', timeout: 2000 })
      )
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
      this.$router.push({ path: '/music/artists/' + this.track.album_artist_id })
    }
  }
}
</script>

<style>
</style>
