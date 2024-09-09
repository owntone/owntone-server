<template>
  <transition name="fade">
    <div v-if="show" class="modal is-active">
      <div class="modal-background" @click="$emit('close')" />
      <div class="modal-content">
        <div class="card">
          <div class="card-content">
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
            <div class="content is-small">
              <p v-if="item.album">
                <span class="heading" v-text="$t('dialog.track.album')" />
                <a
                  class="title is-6 has-text-link"
                  @click="open_album"
                  v-text="item.album"
                />
              </p>
              <p v-if="item.album_artist && item.media_kind !== 'audiobook'">
                <span
                  class="heading"
                  v-text="$t('dialog.track.album-artist')"
                />
                <a
                  class="title is-6 has-text-link"
                  @click="open_album_artist"
                  v-text="item.album_artist"
                />
              </p>
              <p v-if="item.composer">
                <span class="heading" v-text="$t('dialog.track.composer')" />
                <span class="title is-6" v-text="item.composer" />
              </p>
              <p v-if="item.date_released">
                <span
                  class="heading"
                  v-text="$t('dialog.track.release-date')"
                />
                <span
                  class="title is-6"
                  v-text="$filters.date(item.date_released)"
                />
              </p>
              <p v-else-if="item.year">
                <span class="heading" v-text="$t('dialog.track.year')" />
                <span class="title is-6" v-text="item.year" />
              </p>
              <p v-if="item.genre">
                <span class="heading" v-text="$t('dialog.track.genre')" />
                <a
                  class="title is-6 has-text-link"
                  @click="open_genre"
                  v-text="item.genre"
                />
              </p>
              <p v-if="item.disc_number">
                <span class="heading" v-text="$t('dialog.track.position')" />
                <span
                  class="title is-6"
                  v-text="[item.disc_number, item.track_number].join(' / ')"
                />
              </p>
              <p v-if="item.length_ms">
                <span class="heading" v-text="$t('dialog.track.duration')" />
                <span
                  class="title is-6"
                  v-text="$filters.durationInHours(item.length_ms)"
                />
              </p>
              <p>
                <span class="heading" v-text="$t('dialog.track.path')" />
                <span class="title is-6" v-text="item.path" />
              </p>
              <p>
                <span class="heading" v-text="$t('dialog.track.type')" />
                <span
                  class="title is-6"
                  v-text="
                    `${$t(`media.kind.${item.media_kind}`)} - ${$t(`data.kind.${item.data_kind}`)}`
                  "
                />
              </p>
              <p v-if="item.samplerate">
                <span class="heading" v-text="$t('dialog.track.quality')" />
                <span class="title is-6">
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
                </span>
              </p>
              <p>
                <span class="heading" v-text="$t('dialog.track.added-on')" />
                <span
                  class="title is-6"
                  v-text="$filters.datetime(item.time_added)"
                />
              </p>
              <p>
                <span class="heading" v-text="$t('dialog.track.rating')" />
                <span
                  class="title is-6"
                  v-text="
                    $t('dialog.track.rating-value', {
                      rating: Math.floor(item.rating / 10)
                    })
                  "
                />
              </p>
              <p v-if="item.comment">
                <span class="heading" v-text="$t('dialog.track.comment')" />
                <span class="title is-6" v-text="item.comment" />
              </p>
            </div>
          </div>
          <footer class="card-footer">
            <a class="card-footer-item has-text-dark" @click="queue_add">
              <mdicon class="icon" name="playlist-plus" size="16" />
              <span class="is-size-7" v-text="$t('dialog.track.add')" />
            </a>
            <a class="card-footer-item has-text-dark" @click="queue_add_next">
              <mdicon class="icon" name="playlist-play" size="16" />
              <span class="is-size-7" v-text="$t('dialog.track.add-next')" />
            </a>
            <a class="card-footer-item has-text-dark" @click="play">
              <mdicon class="icon" name="play" size="16" />
              <span class="is-size-7" v-text="$t('dialog.track.play')" />
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
</template>

<script>
import SpotifyWebApi from 'spotify-web-api-js'
import { useServicesStore } from '@/stores/services'
import webapi from '@/webapi'

export default {
  name: 'ModalDialogTrack',
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
