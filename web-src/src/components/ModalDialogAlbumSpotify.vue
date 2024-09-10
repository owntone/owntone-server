<template>
  <base-modal :show="show" @close="$emit('close')">
    <template #content>
      <cover-artwork
        :url="artwork_url(item)"
        :artist="item.artist"
        :album="item.name"
        class="fd-has-shadow fd-cover fd-cover-normal-image"
        @load="artwork_loaded"
        @error="artwork_error"
      />
      <p class="title is-4">
        <a class="has-text-link" @click="open" v-text="item.name" />
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
            v-text="item.artists[0].name"
          />
        </p>
        <p>
          <span
            class="heading"
            v-text="$t('dialog.spotify.album.release-date')"
          />
          <span class="title is-6" v-text="$filters.date(item.release_date)" />
        </p>
        <p>
          <span class="heading" v-text="$t('dialog.spotify.album.type')" />
          <span class="title is-6" v-text="item.album_type" />
        </p>
      </div>
    </template>
    <template #footer>
      <a class="card-footer-item has-text-dark" @click="queue_add">
        <mdicon class="icon" name="playlist-plus" size="16" />
        <span class="is-size-7" v-text="$t('dialog.spotify.album.add')" />
      </a>
      <a class="card-footer-item has-text-dark" @click="queue_add_next">
        <mdicon class="icon" name="playlist-play" size="16" />
        <span class="is-size-7" v-text="$t('dialog.spotify.album.add-next')" />
      </a>
      <a class="card-footer-item has-text-dark" @click="play">
        <mdicon class="icon" name="play" size="16" />
        <span class="is-size-7" v-text="$t('dialog.spotify.album.play')" />
      </a>
    </template>
  </base-modal>
</template>

<script>
import BaseModal from '@/components/BaseModal.vue'
import CoverArtwork from '@/components/CoverArtwork.vue'
import webapi from '@/webapi'

export default {
  name: 'ModalDialogAlbumSpotify',
  components: { BaseModal, CoverArtwork },
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
