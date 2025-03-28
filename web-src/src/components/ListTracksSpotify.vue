<template>
  <list-item
    v-for="item in items"
    :key="item.id"
    :is-playable="item.is_playable"
    :lines="[item.name, item.artists[0].name, item.album.name]"
    @open="open(item)"
    @open-details="openDetails(item)"
  >
    <template v-if="!item.is_playable" #reason>
      (<span v-text="$t('list.spotify.not-playable-track')" />
      <span
        v-if="item.restrictions?.reason"
        v-text="
          $t('list.spotify.restriction-reason', {
            reason: item.restrictions.reason
          })
        "
      />)
    </template>
  </list-item>
  <loader-list-item :load="load" :loaded="loaded" />
  <modal-dialog-track-spotify
    :item="selectedItem"
    :show="showDetailsModal"
    @close="showDetailsModal = false"
  />
</template>

<script>
import ListItem from '@/components/ListItem.vue'
import LoaderListItem from '@/components/LoaderListItem.vue'
import ModalDialogTrackSpotify from '@/components/ModalDialogTrackSpotify.vue'
import webapi from '@/webapi'

export default {
  name: 'ListTracksSpotify',
  components: { ListItem, LoaderListItem, ModalDialogTrackSpotify },
  props: {
    contextUri: { default: '', type: String },
    items: { required: true, type: Object },
    load: { default: null, type: Function },
    loaded: { default: true, type: Boolean }
  },
  data() {
    return { selectedItem: {}, showDetailsModal: false }
  },
  methods: {
    open(item) {
      if (item.is_playable) {
        webapi.player_play_uri(
          this.contextUri || item.uri,
          false,
          item.position || 0
        )
      }
    },
    openDetails(item) {
      this.selectedItem = item
      this.showDetailsModal = true
    }
  }
}
</script>
