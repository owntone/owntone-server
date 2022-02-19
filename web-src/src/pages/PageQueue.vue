<template>
  <content-with-heading>
    <template v-slot:heading-left>
      <p class="heading">{{ queue.count }} tracks</p>
      <p class="title is-4">Queue</p>
    </template>
    <template v-slot:heading-right>
      <div class="buttons is-centered">
        <a class="button is-small" :class="{ 'is-info': show_only_next_items }" @click="update_show_next_items">
          <span class="icon">
            <i class="mdi mdi-arrow-collapse-down"></i>
          </span>
          <span>Hide previous</span>
        </a>
        <a class="button is-small" @click="open_add_stream_dialog">
          <span class="icon">
            <i class="mdi mdi-web"></i>
          </span>
          <span>Add Stream</span>
        </a>
        <a class="button is-small" :class="{ 'is-info': edit_mode }" @click="edit_mode = !edit_mode">
          <span class="icon">
            <i class="mdi mdi-pencil"></i>
          </span>
          <span>Edit</span>
        </a>
        <a class="button is-small" @click="queue_clear">
          <span class="icon">
            <i class="mdi mdi-delete-empty"></i>
          </span>
          <span>Clear</span>
        </a>
        <a class="button is-small" v-if="is_queue_save_allowed" :disabled="queue_items.length === 0" @click="save_dialog">
          <span class="icon">
            <i class="mdi mdi-content-save"></i>
          </span>
          <span>Save</span>
        </a>
      </div>
    </template>
    <template v-slot:content>
      <draggable v-model="queue_items" handle=".handle" item-key="id" @end="move_item">
        <template #item="{ element, index }">
          <list-item-queue-item
            :item="element"
            :position="index"
            :current_position="current_position"
            :show_only_next_items="show_only_next_items"
            :edit_mode="edit_mode">
              <template v-slot:actions>
                <a @click.prevent.stop="open_dialog(element)" v-if="!edit_mode">
                  <span class="icon has-text-dark"><i class="mdi mdi-dots-vertical mdi-18px"></i></span>
                </a>
                <a @click.prevent.stop="remove(element)" v-if="element.id !== state.item_id && edit_mode">
                  <span class="icon has-text-grey"><i class="mdi mdi-delete mdi-18px"></i></span>
                </a>
              </template>
            </list-item-queue-item>
        </template>
      </draggable>
      <modal-dialog-queue-item :show="show_details_modal" :item="selected_item" @close="show_details_modal = false" />
      <modal-dialog-add-url-stream :show="show_url_modal" @close="show_url_modal = false" />
      <modal-dialog-playlist-save v-if="is_queue_save_allowed" :show="show_pls_save_modal" @close="show_pls_save_modal = false" />
    </template>
  </content-with-heading>
</template>

<script>
import ContentWithHeading from '@/templates/ContentWithHeading.vue'
import ListItemQueueItem from '@/components/ListItemQueueItem.vue'
import ModalDialogQueueItem from '@/components/ModalDialogQueueItem.vue'
import ModalDialogAddUrlStream from '@/components/ModalDialogAddUrlStream.vue'
import ModalDialogPlaylistSave from '@/components/ModalDialogPlaylistSave.vue'
import webapi from '@/webapi'
import * as types from '@/store/mutation_types'
import draggable from 'vuedraggable'

export default {
  name: 'PageQueue',
  components: { ContentWithHeading, ListItemQueueItem, draggable, ModalDialogQueueItem, ModalDialogAddUrlStream, ModalDialogPlaylistSave },

  data () {
    return {
      edit_mode: false,

      show_details_modal: false,
      show_url_modal: false,
      show_pls_save_modal: false,
      selected_item: {}
    }
  },

  computed: {
    state () {
      return this.$store.state.player
    },
    is_queue_save_allowed () {
      return this.$store.state.config.allow_modifying_stored_playlists && this.$store.state.config.default_playlist_directory
    },
    queue () {
      return this.$store.state.queue
    },
    queue_items: {
      get () { return this.$store.state.queue.items },
      set (value) { /* Do nothing? Send move request in @end event */ }
    },
    current_position () {
      const nowPlaying = this.$store.getters.now_playing
      return nowPlaying === undefined || nowPlaying.position === undefined ? -1 : this.$store.getters.now_playing.position
    },
    show_only_next_items () {
      return this.$store.state.show_only_next_items
    }
  },

  methods: {
    queue_clear: function () {
      webapi.queue_clear()
    },

    update_show_next_items: function (e) {
      this.$store.commit(types.SHOW_ONLY_NEXT_ITEMS, !this.show_only_next_items)
    },

    remove: function (item) {
      webapi.queue_remove(item.id)
    },

    move_item: function (e) {
      const oldPosition = !this.show_only_next_items ? e.oldIndex : e.oldIndex + this.current_position
      const item = this.queue_items[oldPosition]
      const newPosition = item.position + (e.newIndex - e.oldIndex)
      if (newPosition !== oldPosition) {
        webapi.queue_move(item.id, newPosition)
      }
    },

    open_dialog: function (item) {
      this.selected_item = item
      this.show_details_modal = true
    },

    open_add_stream_dialog: function (item) {
      this.show_url_modal = true
    },

    save_dialog: function (item) {
      if (this.queue_items.length > 0) {
        this.show_pls_save_modal = true
      }
    }
  }
}
</script>

<style>
</style>
