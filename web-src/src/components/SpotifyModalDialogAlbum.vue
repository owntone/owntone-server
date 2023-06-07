<template>
  <div>
    <transition name="fade">
      <div v-if="show" class="modal is-active">
        <div class="modal-background" @click="$emit('close')" />
        <div class="modal-content fd-modal-card">
          <div class="card">
            <div class="card-content">
              <cover-artwork
                :artwork_url="artwork_url"
                :artist="album.artist"
                :album="album.name"
                class="fd-has-shadow fd-has-margin-bottom fd-cover fd-cover-normal-image"
                @load="artwork_loaded"
                @error="artwork_error"
              />
              <p class="title is-4">
                <a
                  class="has-text-link"
                  @click="open_album"
                  v-text="album.name"
                />
              </p>
              <div class="content is-small">
                <p>
                  <span
                    class="heading"
                    v-text="$t('dialog.spotify.album.album-artist')"
                  />
                  <a
                    class="title is-6 has-text-link"
                    @click="open_artist"
                    v-text="album.artists[0].name"
                  />
                </p>
                <p>
                  <span
                    class="heading"
                    v-text="$t('dialog.spotify.album.release-date')"
                  />
                  <span
                    class="title is-6"
                    v-text="$filters.date(album.release_date)"
                  />
                </p>
                <p>
                  <span
                    class="heading"
                    v-text="$t('dialog.spotify.album.type')"
                  />
                  <span class="title is-6" v-text="album.album_type" />
                </p>
              </div>
            </div>
            <footer class="card-footer">
              <a class="card-footer-item has-text-dark" @click="queue_add">
                <span class="icon"
                  ><mdicon name="playlist-plus" size="16"
                /></span>
                <span
                  class="is-size-7"
                  v-text="$t('dialog.spotify.album.add')"
                />
              </a>
              <a class="card-footer-item has-text-dark" @click="queue_add_next">
                <span class="icon"
                  ><mdicon name="playlist-play" size="16"
                /></span>
                <span
                  class="is-size-7"
                  v-text="$t('dialog.spotify.album.add-next')"
                />
              </a>
              <a class="card-footer-item has-text-dark" @click="play">
                <span class="icon"><mdicon name="play" size="16" /></span>
                <span
                  class="is-size-7"
                  v-text="$t('dialog.spotify.album.play')"
                />
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
import CoverArtwork from '@/components/CoverArtwork.vue'

export default {
  name: 'SpotifyModalDialogAlbum',
  components: { CoverArtwork },
  props: ['show', 'album'],
  emits: ['close'],

  data() {
    return {
      artwork_visible: false
    }
  },

  computed: {
    artwork_url: function () {
      if (this.album.images && this.album.images.length > 0) {
        return this.album.images[0].url
      }
      return ''
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
      this.$router.push({ path: '/music/spotify/albums/' + this.album.id })
    },

    open_artist: function () {
      this.$router.push({
        path: '/music/spotify/artists/' + this.album.artists[0].id
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
