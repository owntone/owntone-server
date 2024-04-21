<template>
  <transition name="fade">
    <div v-if="show" class="modal is-active">
      <div class="modal-background" @click="$emit('close')" />
      <div class="modal-content">
        <div class="card">
          <div class="card-content">
            <cover-artwork
              :artwork_url="item.artwork_url"
              :artist="item.artist"
              :album="item.name"
              class="fd-has-shadow fd-cover fd-cover-normal-image mb-5"
            />
            <p class="title is-4">
              <a class="has-text-link" @click="open" v-text="item.name" />
            </p>
            <div v-if="media_kind_resolved === 'podcast'" class="buttons">
              <a
                class="button is-small"
                @click="mark_played"
                v-text="$t('dialog.album.mark-as-played')"
              />
              <a
                v-if="item.data_kind === 'url'"
                class="button is-small"
                @click="$emit('remove-podcast')"
                v-text="$t('dialog.album.remove-podcast')"
              />
            </div>
            <div class="content is-small">
              <p v-if="item.artist">
                <span class="heading" v-text="$t('dialog.album.artist')" />
                <a
                  class="title is-6 has-text-link"
                  @click="open_artist"
                  v-text="item.artist"
                />
              </p>
              <p v-if="item.date_released">
                <span
                  class="heading"
                  v-text="$t('dialog.album.release-date')"
                />
                <span
                  class="title is-6"
                  v-text="$filters.date(item.date_released)"
                />
              </p>
              <p v-else-if="item.year">
                <span class="heading" v-text="$t('dialog.album.year')" />
                <span class="title is-6" v-text="item.year" />
              </p>
              <p>
                <span class="heading" v-text="$t('dialog.album.tracks')" />
                <span class="title is-6" v-text="item.track_count" />
              </p>
              <p>
                <span class="heading" v-text="$t('dialog.album.duration')" />
                <span
                  class="title is-6"
                  v-text="$filters.durationInHours(item.length_ms)"
                />
              </p>
              <p>
                <span class="heading" v-text="$t('dialog.album.type')" />
                <span
                  class="title is-6"
                  v-text="
                    `${$t(`media.kind.${item.media_kind}`)} - ${$t(`data.kind.${item.data_kind}`)}`
                  "
                />
              </p>
              <p>
                <span class="heading" v-text="$t('dialog.album.added-on')" />
                <span
                  class="title is-6"
                  v-text="$filters.datetime(item.time_added)"
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
</template>

<script>
import CoverArtwork from '@/components/CoverArtwork.vue'
import webapi from '@/webapi'

export default {
  name: 'ModalDialogAlbum',
  components: { CoverArtwork },
  props: {
    item: { required: true, type: Object },
    media_kind: { default: '', type: String },
    show: Boolean
  },
  emits: ['close', 'remove-podcast', 'play-count-changed'],

  data() {
    return {
      artwork_visible: false
    }
  },

  computed: {
    media_kind_resolved() {
      return this.media_kind || this.item.media_kind
    }
  },

  methods: {
    mark_played() {
      webapi
        .library_album_track_update(this.item.id, { play_count: 'played' })
        .then(() => {
          this.$emit('play-count-changed')
          this.$emit('close')
        })
    },
    open() {
      this.$emit('close')
      if (this.media_kind_resolved === 'podcast') {
        this.$router.push({ name: 'podcast', params: { id: this.item.id } })
      } else if (this.media_kind_resolved === 'audiobook') {
        this.$router.push({
          name: 'audiobooks-album',
          params: { id: this.item.id }
        })
      } else {
        this.$router.push({
          name: 'music-album',
          params: { id: this.item.id }
        })
      }
    },
    open_artist() {
      this.$emit('close')
      if (this.media_kind_resolved === 'audiobook') {
        this.$router.push({
          name: 'audiobooks-artist',
          params: { id: this.item.artist_id }
        })
      } else {
        this.$router.push({
          name: 'music-artist',
          params: { id: this.item.artist_id }
        })
      }
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

<style></style>
