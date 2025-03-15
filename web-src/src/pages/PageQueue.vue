<template>
  <div>
    <content-with-heading>
      <template #heading>
        <heading-title :content="heading" />
      </template>
      <template #actions>
        <control-button
          :button="{
            handler: uiStore.toggleHideReadItems,
            icon: 'eye-off-outline',
            key: 'actions.hide-previous'
          }"
          :class="{ 'is-dark': uiStore.hideReadItems }"
        />
        <control-button
          :button="{
            handler: openAddStreamDialog,
            icon: 'web',
            key: 'actions.add-stream'
          }"
        />
        <control-button
          :button="{
            handler: toggleEdit,
            icon: 'pencil',
            key: 'actions.edit'
          }"
          :class="{ 'is-dark': editing }"
          :disabled="queueStore.isEmpty"
        />
        <control-button
          :button="{
            handler: clearQueue,
            icon: 'delete-empty',
            key: 'actions.clear'
          }"
          :disabled="queueStore.isEmpty"
        />
        <control-button
          v-if="queueStore.isSavingAllowed"
          :button="{
            handler: openSaveDialog,
            icon: 'download',
            key: 'actions.save'
          }"
          :disabled="queueStore.isEmpty"
        />
      </template>
      <template #content>
        <draggable v-model="items" item-key="id" @end="moveItem">
          <template #item="{ element, index }">
            <list-item-queue-item
              :item="element"
              :position="index"
              :current-position="currentPosition"
              :hide-read-items="uiStore.hideReadItems"
              :editing="editing"
            >
              <template #actions>
                <a v-if="!editing" @click.prevent.stop="openDetails(element)">
                  <mdicon
                    class="icon has-text-grey"
                    name="dots-vertical"
                    size="16"
                  />
                </a>
                <a
                  v-if="isRemovable(element)"
                  @click.prevent.stop="remove(element)"
                >
                  <mdicon class="icon has-text-grey" name="delete" size="18" />
                </a>
              </template>
            </list-item-queue-item>
          </template>
        </draggable>
        <modal-dialog-queue-item
          :show="showDetailsModal"
          :item="selectedItem"
          @close="showDetailsModal = false"
        />
        <modal-dialog-add-stream
          :show="showAddStreamDialog"
          @close="showAddStreamDialog = false"
        />
        <modal-dialog-playlist-save
          v-if="queueStore.isSavingAllowed"
          :show="showSaveModal"
          @close="showSaveModal = false"
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
    HeadingTitle,
    ListItemQueueItem,
    ModalDialogAddStream,
    ModalDialogPlaylistSave,
    ModalDialogQueueItem,
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
      editing: false,
      selectedItem: {},
      showDetailsModal: false,
      showSaveModal: false,
      showAddStreamDialog: false
    }
  },
  computed: {
    currentPosition() {
      return this.queueStore.current?.position ?? -1
    },
    heading() {
      return {
        subtitle: [{ count: this.queueStore.count, key: 'count.tracks' }],
        title: this.$t('page.queue.title')
      }
    },
    items: {
      get() {
        return this.queueStore.items
      },
      set(value) {
        /* Do nothing? Send move request in @end event */
      }
    }
  },
  methods: {
    clearQueue() {
      webapi.queue_clear()
    },
    isRemovable(item) {
      return item.id !== this.playerStore.item_id && this.editing
    },
    moveItem(event) {
      const oldPosition =
        event.oldIndex + (this.uiStore.hideReadItems && this.currentPosition)
      const item = this.items[oldPosition]
      const newPosition = item.position + (event.newIndex - event.oldIndex)
      if (newPosition !== oldPosition) {
        webapi.queue_move(item.id, newPosition)
      }
    },
    openAddStreamDialog() {
      this.showAddStreamDialog = true
    },
    openDetails(item) {
      this.selectedItem = item
      this.showDetailsModal = true
    },
    openSaveDialog() {
      if (!this.queueStore.isEmpty) {
        this.showSaveModal = true
      }
    },
    remove(item) {
      webapi.queue_remove(item.id)
    },
    toggleEdit() {
      this.editing = !this.editing
    }
  }
}
</script>
