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
      <draggable v-model="queue_items" :options="{handle:'.handle'}"  @end="move_item">
        <list-item-queue-item v-for="(item, index) in queue_items"
          :key="item.id" :item="item" :position="index"
          :current_position="current_position"
          :show_only_next_items="show_only_next_items"
          :edit_mode="edit_mode"></list-item-queue-item>
      </draggable>
    </template>
  </content-with-heading>
</template>

<script>
import ContentWithHeading from '@/templates/ContentWithHeading'
import ListItemQueueItem from '@/components/ListItemQueueItem'
import webapi from '@/webapi'
import * as types from '@/store/mutation_types'
import draggable from 'vuedraggable'

export default {
  name: 'PageQueue',
  components: { ContentWithHeading, ListItemQueueItem, draggable },

  data () {
    return {
      edit_mode: false
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

    move_item: function (e) {
      var oldPosition = !this.show_only_next_items ? e.oldIndex : e.oldIndex + this.current_position
      var item = this.queue_items[oldPosition]
      var newPosition = item.position + (e.newIndex - e.oldIndex)
      if (newPosition !== oldPosition) {
        webapi.queue_move(item.id, newPosition)
      }
    }
  }
}
</script>

<style>
</style>
