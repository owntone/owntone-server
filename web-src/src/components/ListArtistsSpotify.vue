<template>
  <list-item
    v-for="item in items"
    :key="item.id"
    :is-item="true"
    :index="item.index"
    :lines="[item.name]"
    @open="open(item)"
    @open-details="openDetails(item)"
  />
  <loader-list-item :load="load" :loaded="loaded" />
  <modal-dialog-artist-spotify
    :item="selectedItem"
    :show="showDetailsModal"
    @close="showDetailsModal = false"
  />
</template>

<script>
import ListItem from '@/components/ListItem.vue'
import LoaderListItem from '@/components/LoaderListItem.vue'
import ModalDialogArtistSpotify from '@/components/ModalDialogArtistSpotify.vue'

export default {
  name: 'ListArtistsSpotify',
  components: { ListItem, LoaderListItem, ModalDialogArtistSpotify },
  props: {
    items: { required: true, type: Object },
    load: { default: null, type: Function },
    loaded: { default: true, type: Boolean }
  },
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
    openDetails(item) {
      this.selectedItem = item
      this.showDetailsModal = true
    }
  }
}
</script>
