<template>
  <div class="media" :id="anchor_name">
    <div class="media-content fd-has-action is-clipped" v-on:click="open_artist">
      <h1 class="title is-6">{{ artist.name }}</h1>
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
                <a class="has-text-link" @click="open_artist">{{ artist.name }}</a>
              </p>
              <div class="content is-small">
                <p>
                  <span class="heading">Albums</span>
                  <span class="title is-6">{{ artist.album_count }}</span>
                </p>
                <p>
                  <span class="heading">Tracks</span>
                  <span class="title is-6">{{ artist.track_count }}</span>
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
  name: 'PartArtist',
  components: { ModalDialog },

  props: ['artist', 'links'],

  data () {
    return {
      show_details_modal: false
    }
  },

  computed: {
    anchor_name: function () {
      if (this.links === undefined) {
        return 'idx_nav_undef'
      }

      var name = this.artist.name_sort.charAt(0).toUpperCase()
      var anchr = this.links.find(function (elem) {
        return (elem.n === name)
      })

      if (anchr === null) {
        // shouldnt happen!!
        return 'idx_nav_undef'
      } else {
        // console.log('anchr=' + JSON.stringify(anchr, null, 2))
        return anchr.a
      }
    }
  },

  methods: {
    play: function () {
      this.show_details_modal = false
      webapi.queue_clear().then(() =>
        webapi.queue_add(this.artist.uri).then(() =>
          webapi.player_play()
        )
      )
    },

    queue_add: function () {
      this.show_details_modal = false
      webapi.queue_add(this.artist.uri).then(() =>
        this.$store.dispatch('add_notification', { text: 'Artist tracks appended to queue', type: 'info', timeout: 2000 })
      )
    },

    open_artist: function () {
      this.show_details_modal = false
      this.$router.push({ path: '/music/artists/' + this.artist.id })
    }
  }
}
</script>

<style>
</style>
