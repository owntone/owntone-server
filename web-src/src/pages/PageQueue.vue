<template>
  <content-with-heading>
    <template slot="heading-left">
      <p class="heading">{{ queue.count }} tracks</p>
      <p class="title is-4">Queue</p>
    </template>
    <template slot="heading-right">
      <div class="buttons is-centered">
        <a class="button is-small" :class="{ 'is-info': show_only_next_items }" @click="update_show_next_items">
          <span class="icon">
            <i class="mdi mdi-arrow-collapse-down"></i>
          </span>
          <span>Hide previous</span>
        </a>
        <!--
        <a class="button" :class="{ 'is-info': edit_mode }" @click="edit_mode = !edit_mode">
          <span class="icon">
            <i class="mdi mdi-content-save"></i>
          </span>
          <span>Save</span>
        </a>
        -->
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
      </div>
    </template>
    <template slot="content">
      <draggable
         v-model="queue_items"
         handle=".handle"
         v-bind="dragOptions"
         @start="drag=true"
         @update="move_item"
         @end="drag=false">
        <list-item-queue-item v-for="(item, index) in queue_items"
          :key="item.id" :item="item" :position="index"
          :current_position="current_position"
          :show_only_next_items="show_only_next_items"
          :edit_mode="edit_mode">
            <template slot="actions">
              <a @click="open_dialog(item)" v-if="!edit_mode">
                <span class="icon has-text-dark"><i class="mdi mdi-dots-vertical mdi-18px"></i></span>
              </a>
              <a @click="remove(item)" v-if="item.id !== state.item_id && edit_mode">
                <span class="icon has-text-grey"><i class="mdi mdi-delete mdi-18px"></i></span>
              </a>
            </template>
        </list-item-queue-item>
      </draggable>
      <modal-dialog-queue-item :show="show_details_modal" :item="selected_item" @close="show_details_modal = false" />
    </template>
  </content-with-heading>
</template>

<script>
import ContentWithHeading from '@/templates/ContentWithHeading'
import ListItemQueueItem from '@/components/ListItemQueueItem'
import ModalDialogQueueItem from '@/components/ModalDialogQueueItem'
import webapi from '@/webapi'
import * as types from '@/store/mutation_types'
import draggable from 'vuedraggable'

export default {
  name: 'PageQueue',
  components: { ContentWithHeading, ListItemQueueItem, draggable, ModalDialogQueueItem },

  data () {
    return {
      edit_mode: false,
      drag: false,
      pre_move_queue_items: [],

      show_details_modal: false,
      selected_item: {}
    }
  },

  computed: {
    state () {
      return this.$store.state.player
    },
    queue () {
      return this.$store.state.queue
    },
    queue_items: {
      get () { return this.$store.state.queue.items },
      set (value) {
        this.pre_move_queue_items = this.$store.state.queue.items
        this.$store.state.queue.items = value
        this.$store.commit(types.UPDATE_QUEUE, this.$store.state.queue)
      }
    },
    current_position () {
      const nowPlaying = this.$store.getters.now_playing
      return nowPlaying === undefined || nowPlaying.position === undefined ? -1 : this.$store.getters.now_playing.position
    },
    show_only_next_items () {
      return this.$store.state.show_only_next_items
    },
    dragOptions () {
      return {
        animation: 100,
        group: 'description',
        disabled: false,
        ghostClass: 'ghost'
      }
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
      var oldPosition = !this.show_only_next_items ? e.oldIndex : e.oldIndex + this.current_position
      var item = this.pre_move_queue_items[oldPosition]
      this.pre_move_queue_items = []
      var newPosition = item.position + (e.newIndex - e.oldIndex)
      if (newPosition !== oldPosition) {
        webapi.queue_move(item.id, newPosition)
      }
    },

    open_dialog: function (item) {
      this.selected_item = item
      this.show_details_modal = true
    }
  }
}
</script>

<style>
</style>
