<template>
  <modal-dialog :show="show" @close="$emit('close')">
    <template #content>
      <div class="title is-4" v-text="item.title" />
      <div class="subtitle" v-text="item.artist" />
      <div v-if="item.album" class="mb-3">
        <div
          class="is-size-7 is-uppercase"
          v-text="$t('dialog.queue-item.album')"
        />
        <div class="title is-6">
          <a @click="open_album" v-text="item.album" />
        </div>
      </div>
      <div v-if="item.album_artist" class="mb-3">
        <div
          class="is-size-7 is-uppercase"
          v-text="$t('dialog.queue-item.album-artist')"
        />
        <div class="title is-6">
          <a @click="open_album_artist" v-text="item.album_artist" />
        </div>
      </div>
      <div v-if="item.composer" class="mb-3">
        <div
          class="is-size-7 is-uppercase"
          v-text="$t('dialog.queue-item.composer')"
        />
        <div class="title is-6" v-text="item.composer" />
      </div>
      <div v-if="item.year" class="mb-3">
        <div
          class="is-size-7 is-uppercase"
          v-text="$t('dialog.queue-item.year')"
        />
        <div class="title is-6" v-text="item.year" />
      </div>
      <div v-if="item.genre" class="mb-3">
        <div
          class="is-size-7 is-uppercase"
          v-text="$t('dialog.queue-item.genre')"
        />
        <div class="title is-6">
          <a @click="open_genre" v-text="item.genre" />
        </div>
      </div>
      <div v-if="item.disc_number" class="mb-3">
        <div
          class="is-size-7 is-uppercase"
          v-text="$t('dialog.queue-item.position')"
        />
        <div
          class="title is-6"
          v-text="[item.disc_number, item.track_number].join(' / ')"
        />
      </div>
      <div v-if="item.length_ms" class="mb-3">
        <div
          class="is-size-7 is-uppercase"
          v-text="$t('dialog.queue-item.duration')"
        />
        <div
          class="title is-6"
          v-text="$filters.durationInHours(item.length_ms)"
        />
      </div>
      <div class="mb-3">
        <div
          class="is-size-7 is-uppercase"
          v-text="$t('dialog.queue-item.path')"
        />
        <div class="title is-6" v-text="item.path" />
      </div>
      <div class="mb-3">
        <div
          class="is-size-7 is-uppercase"
          v-text="$t('dialog.queue-item.type')"
        />
        <div class="title is-6">
          <span
            v-text="
              `${$t(`media.kind.${item.media_kind}`)} - ${$t(`data.kind.${item.data_kind}`)}`
            "
          />
        </div>
      </div>
      <div v-if="item.samplerate" class="mb-3">
        <div
          class="is-size-7 is-uppercase"
          v-text="$t('dialog.queue-item.quality')"
        />
        <div class="title is-6">
          <span v-text="item.type" />
          <span
            v-if="item.samplerate"
            v-text="
              $t('dialog.queue-item.samplerate', {
                rate: item.samplerate
              })
            "
          />
          <span
            v-if="item.channels"
            v-text="
              $t('dialog.queue-item.channels', {
                channels: $filters.channels(item.channels)
              })
            "
          />
          <span
            v-if="item.bitrate"
            v-text="$t('dialog.queue-item.bitrate', { rate: item.bitrate })"
          />
        </div>
      </div>
    </template>
    <template #footer>
      <a class="card-footer-item has-text-dark" @click="remove">
        <mdicon class="icon" name="delete" size="16" />
        <span class="is-size-7" v-text="$t('dialog.queue-item.remove')" />
      </a>
      <a class="card-footer-item has-text-dark" @click="play">
        <mdicon class="icon" name="play" size="16" />
        <span class="is-size-7" v-text="$t('dialog.queue-item.play')" />
      </a>
    </template>
  </modal-dialog>
</template>

<script>
import ModalDialog from '@/components/ModalDialog.vue'
import SpotifyWebApi from 'spotify-web-api-js'
import { useServicesStore } from '@/stores/services'
import webapi from '@/webapi'

export default {
  name: 'ModalDialogQueueItem',
  components: { ModalDialog },
  props: { item: { required: true, type: Object }, show: Boolean },
  emits: ['close'],

  setup() {
    return { servicesStore: useServicesStore() }
  },

  data() {
    return {
      spotify_track: {}
    }
  },

  watch: {
    item() {
      if (this.item?.data_kind === 'spotify') {
        const spotifyApi = new SpotifyWebApi()
        spotifyApi.setAccessToken(this.servicesStore.spotify.webapi_token)
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
    open_album() {
      if (this.item.data_kind === 'spotify') {
        this.$router.push({
          name: 'music-spotify-album',
          params: { id: this.spotify_track.album.id }
        })
      } else if (this.item.media_kind === 'podcast') {
        this.$router.push({
          name: 'podcast',
          params: { id: this.item.album_id }
        })
      } else if (this.item.media_kind === 'audiobook') {
        this.$router.push({
          name: 'audiobooks-album',
          params: { id: this.item.album_id }
        })
      } else if (this.item.media_kind === 'music') {
        this.$router.push({
          name: 'music-album',
          params: { id: this.item.album_id }
        })
      }
    },
    open_album_artist() {
      if (this.item.data_kind === 'spotify') {
        this.$router.push({
          name: 'music-spotify-artist',
          params: { id: this.spotify_track.artists[0].id }
        })
      } else if (
        this.item.media_kind === 'music' ||
        this.item.media_kind === 'podcast'
      ) {
        this.$router.push({
          name: 'music-artist',
          params: { id: this.item.album_artist_id }
        })
      } else if (this.item.media_kind === 'audiobook') {
        this.$router.push({
          name: 'audiobooks-artist',
          params: { id: this.item.album_artist_id }
        })
      }
    },
    open_genre() {
      this.$router.push({
        name: 'genre-albums',
        params: { name: this.item.genre },
        query: { media_kind: this.item.media_kind }
      })
    },
    play() {
      this.$emit('close')
      webapi.player_play({ item_id: this.item.id })
    },
    remove() {
      this.$emit('close')
      webapi.queue_remove(this.item.id)
    }
  }
}
</script>
