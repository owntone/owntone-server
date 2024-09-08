<template>
  <template v-for="item in items" :key="item.id">
    <div class="media is-align-items-center">
      <div
        class="media-content"
        :class="{
          'is-clickable': item.is_playable,
          'fd-is-not-allowed': !item.is_playable
        }"
        @click="play(item)"
      >
        <p
          class="title is-6"
          :class="{ 'has-text-grey-light': !item.is_playable }"
          v-text="item.name"
        />
        <p
          class="subtitle is-7 has-text-weight-bold"
          :class="{
            'has-text-grey': item.is_playable,
            'has-text-grey-light': !item.is_playable
          }"
          v-text="item.artists[0].name"
        />
        <p class="subtitle is-7 has-text-grey" v-text="item.album.name" />
        <p v-if="!item.is_playable" class="subtitle is-7">
          (<span v-text="$t('list.spotify.not-playable-track')" />
          <span
            v-if="item.restrictions?.reason"
            v-text="
              $t('list.spotify.restriction-reason', {
                reason: item.restrictions.reason
              })
            "
          />)
        </p>
      </div>
      <div class="media-right">
        <a @click.prevent.stop="open_dialog(item)">
          <mdicon class="icon has-text-dark" name="dots-vertical" size="16" />
        </a>
      </div>
    </div>
  </template>
  <teleport to="#app">
    <modal-dialog-track-spotify
      :item="selected_item"
      :show="show_details_modal"
      @close="show_details_modal = false"
    />
  </teleport>
</template>

<script>
import ModalDialogTrackSpotify from '@/components/ModalDialogTrackSpotify.vue'
import webapi from '@/webapi'

export default {
  name: 'ListTracksSpotify',
  components: { ModalDialogTrackSpotify },
  props: {
    context_uri: { default: '', type: String },
    items: { required: true, type: Object }
  },
  data() {
    return { selected_item: {}, show_details_modal: false }
  },
  methods: {
    open_dialog(item) {
      this.selected_item = item
      this.show_details_modal = true
    },
    play(item) {
      if (item.is_playable) {
        webapi.player_play_uri(
          this.context_uri || item.uri,
          false,
          item.position || 0
        )
      }
    }
  }
}
</script>
