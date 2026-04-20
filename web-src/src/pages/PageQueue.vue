<template>
  <content-with-heading>
    <template #heading>
      <pane-title :content="heading" />
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
          disabled: queueStore.isEmpty,
          handler: toggleEdit,
          icon: 'pencil',
          key: 'actions.edit'
        }"
        :class="{ 'is-dark': editing }"
      />
      <control-button
        :button="{
          disabled: queueStore.isEmpty,
          handler: clearQueue,
          icon: 'trash-can-outline',
          key: 'actions.clear'
        }"
      />
      <control-button
        v-if="queueStore.isSavingAllowed"
        :button="{
          disabled: queueStore.isEmpty,
          handler: openSaveDialog,
          icon: 'download',
          key: 'actions.save'
        }"
      />
    </template>
    <template #content>
      <div
        v-for="(element, index) in items"
        :key="element.id"
        :data-drag-index="index"
        :draggable="editing"
        :class="{
          'is-dragging': isDragged(index),
          'is-drag-over': isDraggedOver(index)
        }"
        @touchstart="onTouchStart($event)"
        @touchmove="onTouchMove($event)"
        @touchend="onTouchEnd"
        @dragstart="onDragStart(index)"
        @dragover="onDragOver($event, index)"
        @drop="onDrop(index)"
      >
        <list-item-queue-item
          v-if="
            currentPosition < 0 ||
            element.position >= currentPosition ||
            !uiStore.hideReadItems
          "
          :item="element"
          :is-current="element.id === playerStore.item_id"
          :is-next="currentPosition < 0 || element.position >= currentPosition"
          :current-position="currentPosition"
          :editing="editing"
        >
          <template #icon>
            <div data-drag-handle>
              <mdicon
                v-if="editing"
                class="media-left icon has-text-grey is-movable"
                name="drag-horizontal"
                size="18"
              />
            </div>
          </template>
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
              <mdicon
                class="icon has-text-grey"
                name="trash-can-outline"
                size="18"
              />
            </a>
          </template>
        </list-item-queue-item>
      </div>
      <div
        v-if="editing"
        :data-drag-index="items.length"
        :class="{ 'is-drag-over': isDraggedOver(items.length) }"
        style="height: 50px"
        @dragover="onDragOver($event, items.length)"
        @drop="onDrop(items.length)"
      >
        <br />
      </div>
    </template>
  </content-with-heading>
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

<script setup>
import { computed, ref } from 'vue'
import ContentWithHeading from '@/templates/ContentWithHeading.vue'
import ControlButton from '@/components/ControlButton.vue'
import ListItemQueueItem from '@/components/ListItemQueueItem.vue'
import ModalDialogAddStream from '@/components/ModalDialogAddStream.vue'
import ModalDialogPlaylistSave from '@/components/ModalDialogPlaylistSave.vue'
import ModalDialogQueueItem from '@/components/ModalDialogQueueItem.vue'
import PaneTitle from '@/components/PaneTitle.vue'
import queue from '@/api/queue'
import { useDraggableList } from '@/lib/DraggableList'
import { useI18n } from 'vue-i18n'
import { usePlayerStore } from '@/stores/player'
import { useQueueStore } from '@/stores/queue'
import { useUIStore } from '@/stores/ui'

const playerStore = usePlayerStore()
const queueStore = useQueueStore()
const uiStore = useUIStore()
const { t } = useI18n()

const editing = ref(false)
const selectedItem = ref({})
const showAddStreamDialog = ref(false)
const showDetailsModal = ref(false)
const showSaveModal = ref(false)

const currentPosition = computed(() => queueStore.current?.position ?? -1)

const items = computed(() => queueStore.items)

const moveItem = (item) => {
  queue.move(items.value[item.from].id, item.to)
}

const {
  isDragged,
  isDraggedOver,
  onDragStart,
  onDragOver,
  onDrop,
  onTouchStart,
  onTouchMove,
  onTouchEnd
} = useDraggableList(moveItem)

const heading = computed(() => ({
  subtitle: [{ count: queueStore.count, key: 'data.tracks' }],
  title: t('page.queue.title')
}))

const clearQueue = () => {
  queue.clear()
}

const isRemovable = (item) => item.id !== playerStore.item_id && editing.value

const openAddStreamDialog = () => {
  showAddStreamDialog.value = true
}

const openDetails = (item) => {
  selectedItem.value = item
  showDetailsModal.value = true
}

const openSaveDialog = () => {
  if (!queueStore.isEmpty) {
    showSaveModal.value = true
  }
}

const remove = (item) => {
  queue.remove(item.id)
}

const toggleEdit = () => {
  editing.value = !editing.value
}
</script>

<style lang="scss" scoped>
.is-dragging {
  opacity: 0.4;
}
.is-drag-over {
  border-top: 2px solid var(--bulma-text);
}
.is-movable {
  cursor: move;
}
</style>
