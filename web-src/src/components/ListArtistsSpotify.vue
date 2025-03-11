<template>
  <template v-for="item in items" :key="item.id">
    <div class="media is-align-items-center is-clickable mb-0">
      <div class="media-content" @click="open(item)">
        <p class="is-size-6 has-text-weight-bold" v-text="item.name" />
      </div>
      <div class="media-right">
        <a @click.prevent.stop="openDialog(item)">
          <mdicon class="icon has-text-grey" name="dots-vertical" size="16" />
        </a>
      </div>
    </div>
  </template>
  <teleport to="#app">
    <modal-dialog-artist-spotify
      :item="selectedItem"
      :show="showDetailsModal"
      @close="showDetailsModal = false"
    />
  </teleport>
</template>

<script>
import ModalDialogArtistSpotify from '@/components/ModalDialogArtistSpotify.vue'

export default {
  name: 'ListArtistsSpotify',
  components: { ModalDialogArtistSpotify },
  props: { items: { required: true, type: Object } },

  data() {
    return { selectedItem: {}, showDetailsModal: false }
  },
  methods: {
    open(item) {
      this.$router.push({
        name: 'music-spotify-artist',
        params: { id: item.id }
      })
    },
    openDialog(item) {
      this.selectedItem = item
      this.showDetailsModal = true
    }
  }
}
</script>
