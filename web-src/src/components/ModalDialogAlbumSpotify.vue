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
  </modal-dialog>
</template>

<script>
import CoverArtwork from '@/components/CoverArtwork.vue'
import ModalDialog from '@/components/ModalDialog.vue'
import webapi from '@/webapi'

export default {
  name: 'ModalDialogAlbumSpotify',
  components: { ModalDialog, CoverArtwork },
  props: { item: { required: true, type: Object }, show: Boolean },
  emits: ['close'],
  data() {
    return {
      artwork_visible: false
    }
  },
  computed: {
    actions() {
      return [
        {
          label: this.$t('dialog.spotify.album.add'),
          event: 'add',
          icon: 'playlist-plus'
        },
        {
          label: this.$t('dialog.spotify.album.add-next'),
          event: 'add-next',
          icon: 'playlist-play'
        },
        {
          label: this.$t('dialog.spotify.album.play'),
          event: 'play',
          icon: 'play'
        }
      ]
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
