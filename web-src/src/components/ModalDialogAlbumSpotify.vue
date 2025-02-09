<template>
  <modal-dialog-playable :item="item" :show="show" @close="$emit('close')">
    <template #content>
      <div class="title is-4">
        <a @click="open" v-text="item.name" />
      </div>
      <cover-artwork
        :url="artwork_url(item)"
        :artist="item.artist"
        :album="item.name"
        class="is-normal mb-3"
        @load="artwork_loaded"
        @error="artwork_error"
      />
      <div class="mb-3">
        <div
          class="is-size-7 is-uppercase"
          v-text="$t('dialog.spotify.album.album-artist')"
        />
        <div class="title is-6">
          <a @click="open_artist" v-text="item.artists[0].name" />
        </div>
      </div>
      <div class="mb-3">
        <div
          class="is-size-7 is-uppercase"
          v-text="$t('dialog.spotify.album.release-date')"
        />
        <div class="title is-6" v-text="$filters.date(item.release_date)" />
      </div>
      <div class="mb-3">
        <div
          class="is-size-7 is-uppercase"
          v-text="$t('dialog.spotify.album.type')"
        />
        <div class="title is-6" v-text="item.album_type" />
      </div>
    </template>
  </modal-dialog-playable>
</template>

<script>
import CoverArtwork from '@/components/CoverArtwork.vue'
import ModalDialogPlayable from '@/components/ModalDialogPlayable.vue'

export default {
  name: 'ModalDialogAlbumSpotify',
  components: { ModalDialogPlayable, CoverArtwork },
  props: { item: { required: true, type: Object }, show: Boolean },
  emits: ['close'],
  data() {
    return {
      artwork_visible: false
    }
  },
  methods: {
    artwork_error() {
      this.artwork_visible = false
    },
    artwork_loaded() {
      this.artwork_visible = true
    },
    artwork_url(item) {
      return item.images?.[0]?.url || ''
    },
    open() {
      this.$emit('close')
      this.$router.push({
        name: 'music-spotify-album',
        params: { id: this.item.id }
      })
    },
    open_artist() {
      this.$emit('close')
      this.$router.push({
        name: 'music-spotify-artist',
        params: { id: this.item.artists[0].id }
      })
    }
  }
}
</script>
