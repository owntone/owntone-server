<template>
  <template v-for="item in items" :key="item.itemId">
    <div v-if="!item.isItem" class="py-5">
      <span
        :id="`index_${item.index}`"
        class="tag is-small has-text-weight-bold"
        v-text="item.index"
      />
    </div>
    <div
      v-else
      class="media is-align-items-center is-clickable mb-0"
      :class="{ 'with-progress': show_progress }"
      @click="play(item.item)"
    >
      <mdicon
        v-if="show_icon"
        class="media-left icon"
        name="file-music-outline"
      />
      <div class="media-content">
        <div
          class="is-size-6 has-text-weight-bold"
          :class="{
            'has-text-grey':
              item.item.media_kind === 'podcast' && item.item.play_count > 0
          }"
          v-text="item.item.title"
        />
        <div
          class="is-size-7 has-text-weight-bold has-text-grey"
          v-text="item.item.artist"
        />
        <div class="is-size-7 has-text-grey" v-text="item.item.album" />
        <progress
          v-if="show_progress && item.item.seek_ms > 0"
          class="progress is-dark"
          :max="item.item.length_ms"
          :value="item.item.seek_ms"
        />
      </div>
      <div class="media-right">
        <a @click.prevent.stop="open_dialog(item.item)">
          <mdicon class="icon has-text-grey" name="dots-vertical" size="16" />
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
    return { selected_item: {}, show_details_modal: false }
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

.media.with-progress {
  margin-top: 0.375rem;
}
</style>
