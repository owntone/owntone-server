<template>
  <div class="media" v-if="is_next || !show_only_next_items">
    <!---->
    <div class="media-left" v-if="edit_mode">
      <span class="icon has-text-grey fd-is-movable handle"><i class="mdi mdi-drag-horizontal mdi-18px"></i></span>
    </div>

    <div class="media-content fd-has-action is-clipped" v-on:click="play">
      <h1 class="title is-6" :class="{ 'has-text-primary': item.id === state.item_id, 'has-text-grey-light': !is_next }">{{ item.title }}</h1>
      <h2 class="subtitle is-7" :class="{ 'has-text-primary': item.id === state.item_id, 'has-text-grey-light': !is_next, 'has-text-grey': is_next && item.id !== state.item_id }"><b>{{ item.artist }}</b></h2>
      <h2 class="subtitle is-7" :class="{ 'has-text-primary': item.id === state.item_id, 'has-text-grey-light': !is_next, 'has-text-grey': is_next && item.id !== state.item_id }">{{ item.album }}</h2>
    </div>
    <div class="media-right">
      <a v-on:click="remove" v-if="item.id !== state.item_id && edit_mode">
        <span class="icon has-text-grey"><i class="mdi mdi-delete mdi-18px"></i></span>
      </a>
      <a @click="show_details_modal = true" v-if="!edit_mode">
        <span class="icon has-text-dark"><i class="mdi mdi-dots-vertical mdi-18px"></i></span>
      </a>
      <modal-dialog v-if="!edit_mode" :show="show_details_modal" @close="show_details_modal = false">
        <template slot="modal-content">
          <div class="card">
            <div class="card-content">
              <p class="title is-4">
                {{ item.title }}
              </p>
              <p class="subtitle">
                {{ item.artist }}
              </p>
              <div class="content is-small">
                <p>
                  <span class="heading">Album</span>
                  <span class="title is-6">{{ item.album }}</span>
                </p>
                <p v-if="item.album_artist">
                  <span class="heading">Album artist</span>
                  <span class="title is-6">{{ item.album_artist }}</span>
                </p>
                <p v-if="item.year > 0">
                  <span class="heading">Year</span>
                  <span class="title is-6">{{ item.year }}</span>
                </p>
                <p>
                  <span class="heading">Genre</span>
                  <span class="title is-6">{{ item.genre }}</span>
                </p>
                <p>
                  <span class="heading">Track / Disc</span>
                  <span class="title is-6">{{ item.track_number }} / {{ item.disc_number }}</span>
                </p>
                <p>
                  <span class="heading">Length</span>
                  <span class="title is-6">{{ item.length_ms | duration }}</span>
                </p>
                <p>
                  <span class="heading">Path</span>
                  <span class="title is-6">{{ item.path }}</span>
                </p>
              </div>
            </div>
            <footer class="card-footer">
              <a class="card-footer-item has-text-dark" @click="remove">
                <span class="icon"><i class="mdi mdi-delete mdi-18px"></i></span> <span>Remove</span>
              </a>
              <a class="card-footer-item has-text-dark" @click="play">
                <span class="icon"><i class="mdi mdi-play mdi-18px"></i></span> <span>Play</span>
              </a>
            </footer>
          </div>
        </template>
      </modal-dialog>
    </div>
  </div>
</template>

<script>
import ModalDialog from '@/components/ModalDialog'
import webapi from '@/webapi'

export default {
  name: 'PartQueueItem',
  components: { ModalDialog },

  props: ['item', 'position', 'current_position', 'show_only_next_items', 'edit_mode'],

  data () {
    return {
      show_details_modal: false
    }
  },

  computed: {
    state () {
      return this.$store.state.player
    },

    is_next () {
      return this.current_position < 0 || this.position >= this.current_position
    }
  },

  methods: {
    remove: function () {
      this.show_details_modal = false
      webapi.queue_remove(this.item.id)
    },

    play: function () {
      this.show_details_modal = false
      webapi.player_playid(this.item.id)
    }
  }
}
</script>

<style>
</style>
