<template>
  <modal-dialog
    :actions="actions"
    :show="show"
    @add="queue_add"
    @add-next="queue_add_next"
    @close="$emit('close')"
    @play="play"
  >
    <template #modal-content>
      <p class="title is-4" v-text="item.title" />
      <p class="subtitle" v-text="item.artist" />
      <div v-if="item.media_kind === 'podcast'" class="buttons">
        <a
          v-if="item.play_count > 0"
          class="button is-small"
          @click="mark_new"
          v-text="$t('dialog.track.mark-as-new')"
        />
        <a
          v-if="item.play_count === 0"
          class="button is-small"
          @click="mark_played"
          v-text="$t('dialog.track.mark-as-played')"
        />
      </div>
      <div v-if="item.album" class="mb-3">
        <div class="is-size-7 is-uppercase" v-text="$t('dialog.track.album')" />
        <div class="title is-6">
          <a @click="open_album" v-text="item.album" />
        </div>
      </div>
      <div
        v-if="item.album_artist && item.media_kind !== 'audiobook'"
        class="mb-3"
      >
        <div
          class="is-size-7 is-uppercase"
          v-text="$t('dialog.track.album-artist')"
        />
        <div class="title is-6">
          <a @click="open_album_artist" v-text="item.album_artist" />
        </div>
      </div>
      <div v-if="item.composer" class="mb-3">
        <div
          class="is-size-7 is-uppercase"
          v-text="$t('dialog.track.composer')"
        />
        <div class="title is-6" v-text="item.composer" />
      </div>
      <div v-if="item.date_released" class="mb-3">
        <div
          class="is-size-7 is-uppercase"
          v-text="$t('dialog.track.release-date')"
        />
        <div class="title is-6" v-text="$filters.date(item.date_released)" />
      </div>
      <div v-else-if="item.year" class="mb-3">
        <div class="is-size-7 is-uppercase" v-text="$t('dialog.track.year')" />
        <div class="title is-6" v-text="item.year" />
      </div>
      <div v-if="item.genre" class="mb-3">
        <div class="is-size-7 is-uppercase" v-text="$t('dialog.track.genre')" />
        <div class="title is-6">
          <a @click="open_genre" v-text="item.genre" />
        </div>
      </div>
      <div v-if="item.disc_number" class="mb-3">
        <div
          class="is-size-7 is-uppercase"
          v-text="$t('dialog.track.position')"
        />
        <div
          class="title is-6"
          v-text="[item.disc_number, item.track_number].join(' / ')"
        />
      </div>
      <div v-if="item.length_ms" class="mb-3">
        <div
          class="is-size-7 is-uppercase"
          v-text="$t('dialog.track.duration')"
        />
        <div
          class="title is-6"
          v-text="$filters.durationInHours(item.length_ms)"
        />
      </div>
      <div class="mb-3">
        <div class="is-size-7 is-uppercase" v-text="$t('dialog.track.path')" />
        <div class="title is-6" v-text="item.path" />
      </div>
      <div class="mb-3">
        <div class="is-size-7 is-uppercase" v-text="$t('dialog.track.type')" />
        <div
          class="title is-6"
          v-text="
            `${$t(`media.kind.${item.media_kind}`)} - ${$t(`data.kind.${item.data_kind}`)}`
          "
        />
      </div>
      <div v-if="item.samplerate" class="mb-3">
        <div
          class="is-size-7 is-uppercase"
          v-text="$t('dialog.track.quality')"
        />
        <div class="title is-6">
          <span v-text="item.type" />
          <span
            v-if="item.samplerate"
            v-text="
              $t('dialog.track.samplerate', {
                rate: item.samplerate
              })
            "
          />
          <span
            v-if="item.channels"
            v-text="
              $t('dialog.track.channels', {
                channels: $filters.channels(item.channels)
              })
            "
          />
          <span
            v-if="item.bitrate"
            v-text="$t('dialog.track.bitrate', { rate: item.bitrate })"
          />
        </div>
      </div>
      <div class="mb-3">
        <div
          class="is-size-7 is-uppercase"
          v-text="$t('dialog.track.added-on')"
        />
        <div class="title is-6" v-text="$filters.datetime(item.time_added)" />
      </div>
      <div>
        <div
          class="is-size-7 is-uppercase"
          v-text="$t('dialog.track.rating')"
        />
        <div
          class="title is-6"
          v-text="
            $t('dialog.track.rating-value', {
              rating: Math.floor(item.rating / 10)
            })
          "
        />
      </div>
      <div v-if="item.comment" class="mb-3">
        <div
          class="is-size-7 is-uppercase"
          v-text="$t('dialog.track.comment')"
        />
        <div class="title is-6" v-text="item.comment" />
      </div>
    </template>
  </modal-dialog>
</template>

<script>
import ModalDialog from '@/components/ModalDialog.vue'
import SpotifyWebApi from 'spotify-web-api-js'
import { useServicesStore } from '@/stores/services'
import webapi from '@/webapi'

export default {
  name: 'ModalDialogTrack',
  components: { ModalDialog },
  props: { item: { required: true, type: Object }, show: Boolean },
  emits: ['close', 'play-count-changed'],
  setup() {
    return { servicesStore: useServicesStore() }
  },
  data() {
    return {
      spotify_track: {}
    }
  },
  computed: {
    actions() {
      return [
        {
          label: this.$t('dialog.track.add'),
          event: 'add',
          icon: 'playlist-plus'
        },
        {
          label: this.$t('dialog.track.add-next'),
          event: 'add-next',
          icon: 'playlist-play'
        },
        {
          label: this.$t('dialog.track.play'),
          event: 'play',
          icon: 'play'
        }
      ]
    }
  },
  watch: {
    item() {
      if (
        this.item &&
        this.item.data_kind === 'spotify' &&
        this.item.media_kind !== 'podcast'
      ) {
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
    mark_new() {
      webapi
        .library_track_update(this.item.id, { play_count: 'reset' })
        .then(() => {
          this.$emit('play-count-changed')
          this.$emit('close')
        })
    },
    mark_played() {
      webapi
        .library_track_update(this.item.id, { play_count: 'increment' })
        .then(() => {
          this.$emit('play-count-changed')
          this.$emit('close')
        })
    },
    open_album() {
      if (
        this.item.data_kind === 'spotify' &&
        this.item.media_kind !== 'podcast'
      ) {
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
      webapi.player_play_uri(this.item.uri, false)
    },
    queue_add() {
      this.$emit('close')
      webapi.queue_add(this.item.uri)
    },
    queue_add_next() {
      this.$emit('close')
      webapi.queue_add_next(this.item.uri)
    }
  }
}
</script>
