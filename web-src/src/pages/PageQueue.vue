<template>
  <div>
    <content-with-heading>
      <template #heading-left>
        <heading-title :content="heading" />
      </template>
      <template #heading-right>
        <control-button
          :button="{
            handler: update_show_next_items,
            icon: 'eye-off-outline',
            key: 'page.queue.hide-previous',
            class: { 'is-dark': show_only_next_items }
          }"
        />
        <control-button
          :button="{
            handler: open_add_stream_dialog,
            icon: 'web',
            key: 'page.queue.add-stream'
          }"
        />
        <control-button
          :button="{
            handler: toggleEdit,
            icon: 'pencil',
            key: 'page.queue.edit',
            disabled: queue_items.length === 0,
            class: { 'is-dark': edit_mode }
          }"
        />
        <control-button
          :button="{
            handler: queue_clear,
            icon: 'delete-empty',
            key: 'page.queue.clear',
            disabled: queue_items.length === 0
          }"
        />
        <control-button
          v-if="is_queue_save_allowed"
          :button="{
            handler: save_dialog,
            icon: 'download',
            key: 'page.queue.save',
            disabled: queue_items.length === 0
          }"
        />
      </template>
      <template #content>
        <draggable v-model="queue_items" item-key="id" @end="move_item">
          <template #item="{ element, index }">
            <list-item-queue-item
              :item="element"
              :position="index"
              :current_position="current_position"
              :show_only_next_items="show_only_next_items"
              :edit_mode="edit_mode"
            >
              <template #actions>
                <a v-if="!edit_mode" @click.prevent.stop="open_dialog(element)">
                  <mdicon
                    class="icon has-text-grey"
                    name="dots-vertical"
                    size="16"
                  />
                </a>
                <a
                  v-if="element.id !== player.item_id && edit_mode"
                  @click.prevent.stop="remove(element)"
                >
                  <mdicon class="icon has-text-grey" name="delete" size="18" />
                </a>
              </template>
            </list-item-queue-item>
          </template>
        </draggable>
        <modal-dialog-queue-item
          :show="show_details_modal"
          :item="selected_item"
          @close="show_details_modal = false"
        />
        <modal-dialog-add-stream
          :show="show_url_modal"
          @close="show_url_modal = false"
        />
        <modal-dialog-playlist-save
          v-if="is_queue_save_allowed"
          :show="show_pls_save_modal"
          @close="show_pls_save_modal = false"
        />
      </template>
    </content-with-heading>
  </div>
</template>

<script>
import ContentWithHeading from '@/templates/ContentWithHeading.vue'
import ControlButton from '@/components/ControlButton.vue'
import HeadingTitle from '@/components/HeadingTitle.vue'
import ListItemQueueItem from '@/components/ListItemQueueItem.vue'
import ModalDialogAddStream from '@/components/ModalDialogAddStream.vue'
import ModalDialogPlaylistSave from '@/components/ModalDialogPlaylistSave.vue'
import ModalDialogQueueItem from '@/components/ModalDialogQueueItem.vue'
import draggable from 'vuedraggable'
import { useConfigurationStore } from '@/stores/configuration'
import { usePlayerStore } from '@/stores/player'
import { useQueueStore } from '@/stores/queue'
import { useUIStore } from '@/stores/ui'
import webapi from '@/webapi'

export default {
  name: 'PageQueue',
  components: {
    ContentWithHeading,
    ControlButton,
    ListItemQueueItem,
    ModalDialogAddStream,
    ModalDialogPlaylistSave,
    ModalDialogQueueItem,
    HeadingTitle,
    draggable
  },
  setup() {
    return {
      configurationStore: useConfigurationStore(),
      playerStore: usePlayerStore(),
      queueStore: useQueueStore(),
      uiStore: useUIStore()
    }
  },
  data() {
    return {
      edit_mode: false,
      selected_item: {},
      show_details_modal: false,
      show_pls_save_modal: false,
      show_url_modal: false
    }
  },
  computed: {
    current_position() {
      return this.queue.current?.position ?? -1
    },
    heading() {
      return {
        title: this.$t('page.queue.title'),
        subtitle: [{ key: 'count.tracks', count: this.queue.count }]
      }
    },
    is_queue_save_allowed() {
      return (
        this.configurationStore.allow_modifying_stored_playlists &&
        this.configurationStore.default_playlist_directory
      )
    },
    queue() {
      return this.queueStore
    },
    queue_items: {
      get() {
        return this.queue.items
      },
      set() {
        /* Do nothing? Send move request in @end event */
      }
    },
    show_only_next_items() {
      return this.uiStore.show_only_next_items
    },
    player() {
      return this.playerStore
    }
  },

  methods: {
    move_item(event) {
      const oldPosition =
        event.oldIndex + (this.show_only_next_items && this.current_position)
      const item = this.queue_items[oldPosition]
      const newPosition = item.position + (event.newIndex - event.oldIndex)
      if (newPosition !== oldPosition) {
        webapi.queue_move(item.id, newPosition)
      }
    },
    open_add_stream_dialog() {
      this.show_url_modal = true
    },
    open_dialog(item) {
      this.selected_item = item
      this.show_details_modal = true
    },
    queue_clear() {
      webapi.queue_clear()
    },
    remove(item) {
      webapi.queue_remove(item.id)
    },
    save_dialog() {
      if (this.queue_items.length > 0) {
        this.show_pls_save_modal = true
      }
    },
    toggleEdit() {
      this.edit_mode = !this.edit_mode
    },
    update_show_next_items() {
      this.uiStore.show_only_next_items = !this.uiStore.show_only_next_items
    }
  }
}
</script>
