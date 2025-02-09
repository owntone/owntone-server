<template>
  <modal-dialog :actions="actions" :show="show" @close="$emit('close')">
    <template #content>
      <div class="title is-4">
        <a @click="open" v-text="item.name" />
      </div>
      <cover-artwork
        :url="item.artwork_url"
        :artist="item.artist"
        :album="item.name"
        class="is-normal mb-3"
      />
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
      <div v-if="item.artist" class="mb-3">
        <div
          class="is-size-7 is-uppercase"
          v-text="$t('dialog.album.artist')"
        />
        <div class="title is-6">
          <a @click="open_artist" v-text="item.artist" />
        </div>
      </div>
      <div v-if="item.date_released" class="mb-3">
        <div
          class="is-size-7 is-uppercase"
          v-text="$t('dialog.album.release-date')"
        />
        <div class="title is-6" v-text="$filters.date(item.date_released)" />
      </div>
      <div v-else-if="item.year" class="mb-3">
        <div class="is-size-7 is-uppercase" v-text="$t('dialog.album.year')" />
        <div class="title is-6" v-text="item.year" />
      </div>
      <div class="mb-3">
        <div
          class="is-size-7 is-uppercase"
          v-text="$t('dialog.album.tracks')"
        />
        <div class="title is-6" v-text="item.track_count" />
      </div>
      <div class="mb-3">
        <div
          class="is-size-7 is-uppercase"
          v-text="$t('dialog.album.duration')"
        />
        <div
          class="title is-6"
          v-text="$filters.durationInHours(item.length_ms)"
        />
      </div>
      <div class="mb-3">
        <div class="is-size-7 is-uppercase" v-text="$t('dialog.album.type')" />
        <div
          class="title is-6"
          v-text="
            `${$t(`media.kind.${item.media_kind}`)} - ${$t(`data.kind.${item.data_kind}`)}`
          "
        />
      </div>
      <div class="mb-3">
        <div
          class="is-size-7 is-uppercase"
          v-text="$t('dialog.album.added-on')"
        />
        <div class="title is-6" v-text="$filters.datetime(item.time_added)" />
      </div>
    </template>
  </modal-dialog>
</template>

<script>
import CoverArtwork from '@/components/CoverArtwork.vue'
import ModalDialog from '@/components/ModalDialog.vue'
import webapi from '@/webapi'

export default {
  name: 'ModalDialogAlbum',
  components: { ModalDialog, CoverArtwork },
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
    actions() {
      return [
        {
          label: this.$t('dialog.album.add'),
          handler: this.queue_add,
          icon: 'playlist-plus'
        },
        {
          label: this.$t('dialog.album.add-next'),
          handler: this.queue_add_next,
          icon: 'playlist-play'
        },
        {
          label: this.$t('dialog.album.play'),
          handler: this.play,
          icon: 'play'
        }
      ]
    },
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
