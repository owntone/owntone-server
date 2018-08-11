<template>
  <div class="media">
    <div class="media-content fd-has-action is-clipped" v-on:click="open_album">
      <h1 class="title is-6">{{ album.name }}</h1>
      <h2 class="subtitle is-7 has-text-grey"><b>{{ album.artist }}</b></h2>
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
                <a class="has-text-link" @click="open_album">{{ album.name }}</a>
              </p>
              <div class="content is-small">
                <p v-if="album.artist && media_kind !== 'audiobook'">
                  <span class="heading">Album artist</span>
                  <a class="title is-6 has-text-link" @click="open_artist">{{ album.artist }}</a>
                </p>
                <p v-if="album.artist && media_kind === 'audiobook'">
                  <span class="heading">Album artist</span>
                  <span class="title is-6">{{ album.artist }}</span>
                </p>
                <p>
                  <span class="heading">Tracks</span>
                  <span class="title is-6">{{ album.track_count }}</span>
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
  name: 'ListItemAlbum',
  components: { ModalDialog },

  props: ['album', 'media_kind'],

  data () {
    return {
      show_details_modal: false
    }
  },

  methods: {
    play: function () {
      this.show_details_modal = false
      webapi.queue_clear().then(() =>
        webapi.queue_add(this.album.uri).then(() =>
          webapi.player_play()
        )
      )
    },

    queue_add: function () {
      this.show_details_modal = false
      webapi.queue_add(this.album.uri).then(() =>
        this.$store.dispatch('add_notification', { text: 'Album tracks appended to queue', type: 'info', timeout: 2000 })
      )
    },

    open_album: function () {
      this.show_details_modal = false
      if (this.media_kind === 'podcast') {
        this.$router.push({ path: '/podcasts/' + this.album.id })
      } else if (this.media_kind === 'audiobook') {
        this.$router.push({ path: '/audiobooks/' + this.album.id })
      } else {
        this.$router.push({ path: '/music/albums/' + this.album.id })
      }
    },

    open_artist: function () {
      this.show_details_modal = false
      this.$router.push({ path: '/music/artists/' + this.album.artist_id })
    }
  }
}
</script>

<style>
</style>
