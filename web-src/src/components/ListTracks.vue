<template>
  <template v-for="item in items" :key="item.itemId">
    <div v-if="!item.isItem" class="mt-6 mb-5 py-2">
      <span
        :id="'index_' + item.index"
        class="tag is-info is-light is-small has-text-weight-bold"
        v-text="item.index"
      />
    </div>
    <div
      v-else
      class="media is-align-items-center"
      :class="{ 'with-progress': show_progress }"
      @click="play(item.item)"
    >
      <figure v-if="show_icon" class="media-left is-clickable">
        <mdicon class="icon" name="file-outline" size="16" />
      </figure>
      <div class="media-content is-clickable is-clipped">
        <h1
          class="title is-6"
          :class="{
            'has-text-grey':
              item.item.media_kind === 'podcast' && item.item.play_count > 0
          }"
          v-text="item.item.title"
        />
        <h2
          class="subtitle is-7 has-text-grey has-text-weight-bold"
          v-text="item.item.artist"
        />
        <h2 class="subtitle is-7 has-text-grey" v-text="item.item.album" />
        <progress
          v-if="show_progress && item.item.seek_ms > 0"
          class="progress is-info"
          :max="item.item.length_ms"
          :value="item.item.seek_ms"
        />
      </div>
      <div class="media-right">
        <a @click.prevent.stop="open_dialog(item.item)">
          <mdicon class="icon has-text-dark" name="dots-vertical" size="16" />
        </a>
      </div>
    </div>
  </template>
  <teleport to="#app">
    <modal-dialog-track
      :item="selected_item"
      :show="show_details_modal"
      @close="show_details_modal = false"
      @play-count-changed="$emit('play-count-changed')"
    />
  </teleport>
</template>

<script>
import ModalDialogTrack from '@/components/ModalDialogTrack.vue'
import webapi from '@/webapi'

export default {
  name: 'ListTracks',
  components: { ModalDialogTrack },
  props: {
    expression: { default: '', type: String },
    items: { required: true, type: Object },
    show_icon: Boolean,
    show_progress: Boolean,
    uris: { default: '', type: String }
  },
  emits: ['play-count-changed'],

  data() {
    return {
      selected_item: {},
      show_details_modal: false
    }
  },

  methods: {
    open_dialog(item) {
      this.selected_item = item
      this.show_details_modal = true
    },
    play(item) {
      if (this.uris) {
        webapi.player_play_uri(this.uris, false, this.items.items.indexOf(item))
      } else if (this.expression) {
        webapi.player_play_expression(
          this.expression,
          false,
          this.items.items.indexOf(item)
        )
      } else {
        webapi.player_play_uri(item.uri, false)
      }
    }
  }
}
</script>

<style scoped>
.progress {
  height: 0.25rem;
}
</style>
