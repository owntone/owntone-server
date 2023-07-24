<template>
  <div>
    <transition name="fade">
      <div v-if="show" class="modal is-active">
        <div class="modal-background" @click="$emit('close')" />
        <div class="modal-content fd-modal-card">
          <div class="card">
            <div class="card-content">
              <p class="title is-4" v-text="track.title" />
              <p class="subtitle" v-text="track.artist" />
              <div v-if="track.media_kind === 'podcast'" class="buttons">
                <a
                  v-if="track.play_count > 0"
                  class="button is-small"
                  @click="mark_new"
                  v-text="$t('dialog.track.mark-as-new')"
                />
                <a
                  v-if="track.play_count === 0"
                  class="button is-small"
                  @click="mark_played"
                  v-text="$t('dialog.track.mark-as-played')"
                />
              </div>
              <div class="content is-small">
                <p>
                  <span class="heading" v-text="$t('dialog.track.album')" />
                  <a
                    class="title is-6 has-text-link"
                    @click="open_album"
                    v-text="track.album"
                  />
                </p>
                <p
                  v-if="track.album_artist && track.media_kind !== 'audiobook'"
                >
                  <span
                    class="heading"
                    v-text="$t('dialog.track.album-artist')"
                  />
                  <a
                    class="title is-6 has-text-link"
                    @click="open_artist"
                    v-text="track.album_artist"
                  />
                </p>
                <p v-if="track.composer">
                  <span class="heading" v-text="$t('dialog.track.composer')" />
                  <span class="title is-6" v-text="track.composer" />
                </p>
                <p v-if="track.date_released">
                  <span
                    class="heading"
                    v-text="$t('dialog.track.release-date')"
                  />
                  <span
                    class="title is-6"
                    v-text="$filters.date(track.date_released)"
                  />
                </p>
                <p v-else-if="track.year > 0">
                  <span class="heading" v-text="$t('dialog.track.year')" />
                  <span class="title is-6" v-text="track.year" />
                </p>
                <p v-if="track.genre">
                  <span class="heading" v-text="$t('dialog.track.genre')" />
                  <a
                    class="title is-6 has-text-link"
                    @click="open_genre"
                    v-text="track.genre"
                  />
                </p>
                <p>
                  <span class="heading" v-text="$t('dialog.track.position')" />
                  <span
                    class="title is-6"
                    v-text="[track.disc_number, track.track_number].join(' / ')"
                  />
                </p>
                <p>
                  <span class="heading" v-text="$t('dialog.track.duration')" />
                  <span
                    class="title is-6"
                    v-text="$filters.durationInHours(track.length_ms)"
                  />
                </p>
                <p>
                  <span class="heading" v-text="$t('dialog.track.path')" />
                  <span class="title is-6" v-text="track.path" />
                </p>
                <p>
                  <span class="heading" v-text="$t('dialog.track.type')" />
                  <span class="title is-6">
                    <span
                      v-text="
                        [
                          $t('media.kind.' + track.media_kind),
                          $t('data.kind.' + track.data_kind)
                        ].join(' - ')
                      "
                    />
                    <span
                      v-if="track.data_kind === 'spotify'"
                      class="has-text-weight-normal"
                    >
                      (<a
                        @click="open_spotify_artist"
                        v-text="$t('dialog.track.spotify-artist')"
                      />,
                      <a
                        @click="open_spotify_album"
                        v-text="$t('dialog.track.spotify-album')"
                      />)
                    </span>
                  </span>
                </p>
                <p>
                  <span class="heading" v-text="$t('dialog.track.quality')" />
                  <span class="title is-6">
                    <span v-text="track.type" />
                    <span
                      v-if="track.samplerate"
                      v-text="
                        $t('dialog.track.samplerate', {
                          rate: track.samplerate
                        })
                      "
                    />
                    <span
                      v-if="track.channels"
                      v-text="
                        $t('dialog.track.channels', {
                          channels: $filters.channels(track.channels)
                        })
                      "
                    />
                    <span
                      v-if="track.bitrate"
                      v-text="
                        $t('dialog.track.bitrate', { rate: track.bitrate })
                      "
                    />
                  </span>
                </p>
                <p>
                  <span class="heading" v-text="$t('dialog.track.added-on')" />
                  <span
                    class="title is-6"
                    v-text="$filters.datetime(track.time_added)"
                  />
                </p>
                <p>
                  <span class="heading" v-text="$t('dialog.track.rating')" />
                  <span
                    class="title is-6"
                    v-text="
                      $t('dialog.track.rating-value', {
                        rating: Math.floor(track.rating / 10)
                      })
                    "
                  />
                </p>
                <p v-if="track.comment">
                  <span class="heading" v-text="$t('dialog.track.comment')" />
                  <span class="title is-6" v-text="track.comment" />
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
              <a class="card-footer-item has-text-dark" @click="play_track">
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
  </div>
</template>

<script>
import SpotifyWebApi from 'spotify-web-api-js'
import webapi from '@/webapi'

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
    play_track() {
      this.$emit('close')
      webapi.player_play_uri(this.track.uri, false)
    },

    queue_add() {
      this.$emit('close')
      webapi.queue_add(this.track.uri)
    },

    queue_add_next() {
      this.$emit('close')
      webapi.queue_add_next(this.track.uri)
    },

    open_album() {
      this.$emit('close')
      if (this.track.media_kind === 'podcast') {
        this.$router.push({
          name: 'podcast',
          params: { id: this.track.album_id }
        })
      } else if (this.track.media_kind === 'audiobook') {
        this.$router.push({
          name: 'audiobooks-album',
          params: { id: this.track.album_id }
        })
      } else {
        this.$router.push({
          name: 'music-album',
          params: { id: this.track.album_id }
        })
      }
    },

    open_artist() {
      this.$emit('close')
      this.$router.push({
        name: 'music-artist',
        params: { id: this.track.album_artist_id }
      })
    },

    open_genre() {
      this.$router.push({
        name: 'genre-albums',
        params: { name: this.track.genre },
        query: { media_kind: this.track.media_kind }
      })
    },

    open_spotify_artist() {
      this.$emit('close')
      this.$router.push({
        name: 'music-spotify-artist',
        params: { id: this.spotify_track.artists[0].id }
      })
    },

    open_spotify_album() {
      this.$emit('close')
      this.$router.push({
        name: 'music-spotify-album',
        params: { id: this.spotify_track.album.id }
      })
    },

    mark_new() {
      webapi
        .library_track_update(this.track.id, { play_count: 'reset' })
        .then(() => {
          this.$emit('play-count-changed')
          this.$emit('close')
        })
    },

    mark_played() {
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
