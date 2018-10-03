<template>
  <div class="media">
    <div class="media-content fd-has-action is-clipped" v-on:click="open_playlist">
      <h1 class="title is-6">{{ playlist.name }}</h1>
    </div>
    <div class="media-right">
      <a @click="show_details_modal = true">
        <span class="icon has-text-dark"><i class="mdi mdi-dots-vertical mdi-18px"></i></span>
      </a>
      <modal-dialog :show="show_details_modal" @close="show_details_modal = false">
        <template slot="modal-content">
          <div class="card">
            <div class="card-content">
              <p class="title is-4">
                <a class="has-text-link" @click="open_playlist">{{ playlist.name }}</a>
              </p>
              <div class="content is-small">
                <p>
                  <span class="heading">Path</span>
                  <span class="title is-6">{{ playlist.path }}</span>
                </p>
              </div>
            </div>
            <footer class="card-footer">
              <a class="card-footer-item has-text-dark" @click="queue_add">
                <span class="icon"><i class="mdi mdi-playlist-plus mdi-18px"></i></span> <span>Add</span>
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
  name: 'PartPlaylist',
  components: { ModalDialog },

  props: ['playlist'],

  data () {
    return {
      show_details_modal: false
    }
  },

  methods: {
    play: function () {
      this.show_details_modal = false
      webapi.queue_clear().then(() =>
        webapi.queue_add(this.playlist.uri).then(() =>
          webapi.player_play()
        )
      )
    },

    queue_add: function () {
      this.show_details_modal = false
      webapi.queue_add(this.playlist.uri).then(() =>
        this.$store.dispatch('add_notification', { text: 'Playlist appended to queue', type: 'info', timeout: 2000 })
      )
    },

    open_playlist: function () {
      this.show_details_modal = false
      this.$router.push({ path: '/playlists/' + this.playlist.id })
    }
  }
}
</script>

<style>
</style>
