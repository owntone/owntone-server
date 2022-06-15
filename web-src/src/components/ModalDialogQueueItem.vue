<template>
  <div>
    <transition name="fade">
      <div v-if="show" class="modal is-active">
        <div class="modal-background" @click="$emit('close')" />
        <div class="modal-content fd-modal-card">
          <div class="card">
            <div class="card-content">
              <p class="title is-4" v-text="item.title" />
              <p class="subtitle" v-text="item.artist" />
              <div class="content is-small">
                <p>
                  <span
                    class="heading"
                    v-text="$t('dialog.queue-item.album')"
                  />
                  <a
                    v-if="item.album_id"
                    class="title is-6 has-text-link"
                    @click="open_album"
                    v-text="item.album"
                  />
                  <span v-else class="title is-6" v-text="item.album" />
                </p>
                <p v-if="item.album_artist">
                  <span
                    class="heading"
                    v-text="$t('dialog.queue-item.album-artist')"
                  />
                  <a
                    v-if="item.album_artist_id"
                    class="title is-6 has-text-link"
                    @click="open_album_artist"
                    v-text="item.album_artist"
                  />
                  <span v-else class="title is-6" v-text="item.album_artist" />
                </p>
                <p v-if="item.composer">
                  <span
                    class="heading"
                    v-text="$t('dialog.queue-item.composer')"
                  />
                  <span class="title is-6" v-text="item.composer" />
                </p>
                <p v-if="item.year > 0">
                  <span class="heading" v-text="$t('dialog.queue-item.year')" />
                  <span class="title is-6" v-text="item.year" />
                </p>
                <p v-if="item.genre">
                  <span
                    class="heading"
                    v-text="$t('dialog.queue-item.genre')"
                  />
                  <a
                    class="title is-6 has-text-link"
                    @click="open_genre"
                    v-text="item.genre"
                  />
                </p>
                <p>
                  <span
                    class="heading"
                    v-text="$t('dialog.queue-item.position')"
                  />
                  <span
                    class="title is-6"
                    v-text="[item.disc_number, item.track_number].join(' / ')"
                  />
                </p>
                <p>
                  <span
                    class="heading"
                    v-text="$t('dialog.queue-item.duration')"
                  />
                  <span
                    class="title is-6"
                    v-text="$filters.durationInHours(item.length_ms)"
                  />
                </p>
                <p>
                  <span class="heading" v-text="$t('dialog.queue-item.path')" />
                  <span class="title is-6" v-text="item.path" />
                </p>
                <p>
                  <span class="heading" v-text="$t('dialog.queue-item.type')" />
                  <span class="title is-6">
                    <span
                      v-text="[item.media_kind, item.data_kind].join(' - ')"
                    />
                    <span
                      v-if="item.data_kind === 'spotify'"
                      class="has-text-weight-normal"
                    >
                      (<a
                        @click="open_spotify_artist"
                        v-text="$t('dialog.queue-item.spotify-artist')"
                      />,
                      <a
                        @click="open_spotify_album"
                        v-text="$t('dialog.queue-item.spotify-album')"
                      />)
                    </span>
                  </span>
                </p>
                <p>
                  <span
                    class="heading"
                    v-text="$t('dialog.queue-item.quality')"
                  />
                  <span class="title is-6">
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
                      v-text="
                        $t('dialog.queue-item.bitrate', { rate: item.bitrate })
                      "
                    />
                  </span>
                </p>
              </div>
            </div>
            <footer class="card-footer">
              <a class="card-footer-item has-text-dark" @click="remove">
                <span class="icon"><mdicon name="delete" size="16" /></span>
                <span
                  class="is-size-7"
                  v-text="$t('dialog.queue-item.remove')"
                />
              </a>
              <a class="card-footer-item has-text-dark" @click="play">
                <span class="icon"><mdicon name="play" size="16" /></span>
                <span class="is-size-7" v-text="$t('dialog.queue-item.play')" />
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
  emits: ['close'],

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
