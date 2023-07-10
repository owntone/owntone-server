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
                class="fd-has-shadow fd-cover fd-cover-normal-image mb-5"
              />
              <p class="title is-4">
                <a
                  class="has-text-link"
                  @click="open_album"
                  v-text="album.name"
                />
              </p>
              <div v-if="media_kind_resolved === 'podcast'" class="buttons">
                <a
                  class="button is-small"
                  @click="mark_played"
                  v-text="$t('dialog.album.mark-as-played')"
                />
                <a
                  v-if="album.data_kind === 'url'"
                  class="button is-small"
                  @click="$emit('remove-podcast')"
                  v-text="$t('dialog.album.remove-podcast')"
                />
              </div>
              <div class="content is-small">
                <p v-if="album.artist">
                  <span class="heading" v-text="$t('dialog.album.artist')" />
                  <a
                    class="title is-6 has-text-link"
                    @click="open_artist"
                    v-text="album.artist"
                  />
                </p>
                <p v-if="album.date_released">
                  <span
                    class="heading"
                    v-text="$t('dialog.album.release-date')"
                  />
                  <span
                    class="title is-6"
                    v-text="$filters.date(album.date_released)"
                  />
                </p>
                <p v-else-if="album.year > 0">
                  <span class="heading" v-text="$t('dialog.album.year')" />
                  <span class="title is-6" v-text="album.year" />
                </p>
                <p>
                  <span class="heading" v-text="$t('dialog.album.tracks')" />
                  <span class="title is-6" v-text="album.track_count" />
                </p>
                <p>
                  <span class="heading" v-text="$t('dialog.album.duration')" />
                  <span
                    class="title is-6"
                    v-text="$filters.durationInHours(album.length_ms)"
                  />
                </p>
                <p>
                  <span class="heading" v-text="$t('dialog.album.type')" />
                  <span
                    class="title is-6"
                    v-text="
                      [
                        $t('media.kind.' + album.media_kind),
                        $t('data.kind.' + album.data_kind)
                      ].join(' - ')
                    "
                  />
                </p>
                <p>
                  <span class="heading" v-text="$t('dialog.album.added-on')" />
                  <span
                    class="title is-6"
                    v-text="$filters.datetime(album.time_added)"
                  />
                </p>
              </div>
            </div>
            <footer class="card-footer">
              <a class="card-footer-item has-text-dark" @click="queue_add">
                <mdicon class="icon" name="playlist-plus" size="16" />
                <span class="is-size-7" v-text="$t('dialog.album.add')" />
              </a>
              <a class="card-footer-item has-text-dark" @click="queue_add_next">
                <mdicon class="icon" name="playlist-play" size="16" />
                <span class="is-size-7" v-text="$t('dialog.album.add-next')" />
              </a>
              <a class="card-footer-item has-text-dark" @click="play">
                <mdicon class="icon" name="play" size="16" />
                <span class="is-size-7" v-text="$t('dialog.album.play')" />
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
    artwork_url() {
      return webapi.artwork_url_append_size_params(this.album.artwork_url)
    },

    media_kind_resolved() {
      return this.media_kind ? this.media_kind : this.album.media_kind
    }
  },

  methods: {
    play() {
      this.$emit('close')
      webapi.player_play_uri(this.album.uri, false)
    },

    queue_add() {
      this.$emit('close')
      webapi.queue_add(this.album.uri)
    },

    queue_add_next() {
      this.$emit('close')
      webapi.queue_add_next(this.album.uri)
    },

    open_album() {
      this.$emit('close')
      if (this.media_kind_resolved === 'podcast') {
        this.$router.push({ name: 'podcast', params: { id: this.album.id } })
      } else if (this.media_kind_resolved === 'audiobook') {
        this.$router.push({
          name: 'audiobooks-album',
          params: { id: this.album.id }
        })
      } else {
        this.$router.push({
          name: 'music-album',
          params: { id: this.album.id }
        })
      }
    },

    open_artist() {
      this.$emit('close')
      if (this.media_kind_resolved === 'audiobook') {
        this.$router.push({
          name: 'audiobooks-artist',
          params: { id: this.album.artist_id }
        })
      } else {
        this.$router.push({
          name: 'music-artist',
          params: { id: this.album.artist_id }
        })
      }
    },

    mark_played() {
      webapi
        .library_album_track_update(this.album.id, { play_count: 'played' })
        .then(({ data }) => {
          this.$emit('play-count-changed')
          this.$emit('close')
        })
    },

    artwork_loaded() {
      this.artwork_visible = true
    },

    artwork_error() {
      this.artwork_visible = false
    }
  }
}
</script>

<style></style>
