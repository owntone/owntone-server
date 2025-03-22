<template>
  <template v-for="item in items" :key="item.id">
    <div class="media is-align-items-center mb-0">
      <div
        class="media-content"
        :class="{
          'is-clickable': item.is_playable,
          'is-not-allowed': !item.is_playable
        }"
        @click="play(item)"
      >
        <div
          class="is-size-6 has-text-weight-bold"
          :class="{ 'has-text-grey-light': !item.is_playable }"
          v-text="item.name"
        />
        <div
          class="is-size-7 has-text-weight-bold"
          :class="{
            'has-text-grey': item.is_playable,
            'has-text-grey-light': !item.is_playable
          }"
          v-text="item.artists[0].name"
        />
        <div class="is-size-7 has-text-grey" v-text="item.album.name" />
        <div v-if="!item.is_playable" class="is-size-7 has-text-grey">
          (<span v-text="$t('list.spotify.not-playable-track')" />
          <span
            v-if="item.restrictions?.reason"
            v-text="
              $t('list.spotify.restriction-reason', {
                reason: item.restrictions.reason
              })
            "
          />)
        </div>
      </div>
      <div class="media-right">
        <a @click.prevent.stop="openDetails(item)">
          <mdicon class="icon has-text-grey" name="dots-vertical" size="16" />
        </a>
      </div>
    </div>
  </template>
  <modal-dialog-track-spotify
    :item="selectedItem"
    :show="showDetailsModal"
    @close="showDetailsModal = false"
  />
</template>

<script>
import ModalDialogTrackSpotify from '@/components/ModalDialogTrackSpotify.vue'
import webapi from '@/webapi'

export default {
  name: 'ListTracksSpotify',
  components: { ModalDialogTrackSpotify },
  props: {
    contextUri: { default: '', type: String },
    items: { required: true, type: Object }
  },
  data() {
    return { selectedItem: {}, showDetailsModal: false }
  },
  methods: {
    openDetails(item) {
      this.selectedItem = item
      this.showDetailsModal = true
    },
    play(item) {
      if (item.is_playable) {
        webapi.player_play_uri(
          this.contextUri || item.uri,
          false,
          item.position || 0
        )
      }
    }
  }
}
</script>

<style scoped>
.is-not-allowed {
  cursor: not-allowed;
}
</style>
