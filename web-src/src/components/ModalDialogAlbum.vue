<template>
  <div>
    <transition name="fade">
      <div v-if="show" class="modal is-active">
        <div class="modal-background" @click="$emit('close')" />
        <div class="modal-content fd-modal-card">
          <div class="card">
            <div class="card-content">
              <cover-artwork
                :artwork_url="album.artwork_url"
                :artist="album.artist"
                :album="album.name"
                class="image is-square fd-has-margin-bottom fd-has-shadow"
              />
              <p class="title is-4">
                <a class="has-text-link" @click="open_album">{{
                  album.name
                }}</a>
              </p>
              <div v-if="media_kind_resolved === 'podcast'" class="buttons">
                <a class="button is-small" @click="mark_played"
                  >Mark as played</a
                >
                <a class="button is-small" @click="$emit('remove-podcast')"
                  >Remove podcast</a
                >
              </div>
              <div class="content is-small">
                <p v-if="album.artist">
                  <span class="heading">Album artist</span>
                  <a class="title is-6 has-text-link" @click="open_artist">{{
                    album.artist
                  }}</a>
                </p>
                <p v-if="album.date_released">
                  <span class="heading">Release date</span>
                  <span class="title is-6">{{
                    $filters.time(album.date_released, 'L')
                  }}</span>
                </p>
                <p v-else-if="album.year > 0">
                  <span class="heading">Year</span>
                  <span class="title is-6">{{ album.year }}</span>
                </p>
                <p>
                  <span class="heading">Tracks</span>
                  <span class="title is-6">{{ album.track_count }}</span>
                </p>
                <p>
                  <span class="heading">Length</span>
                  <span class="title is-6">{{
                    $filters.duration(album.length_ms)
                  }}</span>
                </p>
                <p>
                  <span class="heading">Type</span>
                  <span class="title is-6"
                    >{{ album.media_kind }} - {{ album.data_kind }}</span
                  >
                </p>
                <p>
                  <span class="heading">Added at</span>
                  <span class="title is-6">{{
                    $filters.time(album.time_added, 'L LT')
                  }}</span>
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
import CoverArtwork from '@/components/CoverArtwork.vue'
import webapi from '@/webapi'

export default {
  name: 'ModalDialogAlbum',
  components: { CoverArtwork },
  props: ['show', 'album', 'media_kind', 'new_tracks'],
  emits: ['close', 'remove-podcast', 'play-count-changed'],

  data() {
    return {
      artwork_visible: false
    }
  },

  computed: {
    artwork_url: function () {
      return webapi.artwork_url_append_size_params(this.album.artwork_url)
    },

    media_kind_resolved: function () {
      return this.media_kind ? this.media_kind : this.album.media_kind
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
      if (this.media_kind_resolved === 'podcast') {
        this.$router.push({ path: '/podcasts/' + this.album.id })
      } else if (this.media_kind_resolved === 'audiobook') {
        this.$router.push({ path: '/audiobooks/' + this.album.id })
      } else {
        this.$router.push({ path: '/music/albums/' + this.album.id })
      }
    },

    open_artist: function () {
      if (this.media_kind_resolved === 'podcast') {
        // No artist page for podcasts
      } else if (this.media_kind_resolved === 'audiobook') {
        this.$router.push({
          path: '/audiobooks/artists/' + this.album.artist_id
        })
      } else {
        this.$router.push({ path: '/music/artists/' + this.album.artist_id })
      }
    },

    mark_played: function () {
      webapi
        .library_album_track_update(this.album.id, { play_count: 'played' })
        .then(({ data }) => {
          this.$emit('play-count-changed')
          this.$emit('close')
        })
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

<style></style>
