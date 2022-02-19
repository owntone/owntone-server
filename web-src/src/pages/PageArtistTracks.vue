<template>
  <div>
    <content-with-heading>
      <template v-slot:options>
        <index-button-list :index="index_list"></index-button-list>
      </template>
      <template v-slot:heading-left>
        <p class="title is-4">{{ artist.name }}</p>
      </template>
      <template v-slot:heading-right>
        <div class="buttons is-centered">
          <a class="button is-small is-light is-rounded" @click="show_artist_details_modal = true">
            <span class="icon"><i class="mdi mdi-dots-horizontal mdi-18px"></i></span>
          </a>
          <a class="button is-small is-dark is-rounded" @click="play">
            <span class="icon"><i class="mdi mdi-shuffle"></i></span> <span>Shuffle</span>
          </a>
        </div>
      </template>
      <template v-slot:content>
        <p class="heading has-text-centered-mobile"><a class="has-text-link" @click="open_artist">{{ artist.album_count }} albums</a> | {{ artist.track_count }} tracks</p>
        <list-tracks :tracks="tracks.items" :uris="track_uris"></list-tracks>
        <modal-dialog-artist :show="show_artist_details_modal" :artist="artist" @close="show_artist_details_modal = false" />
      </template>
    </content-with-heading>
  </div>
</template>

<script>
import ContentWithHeading from '@/templates/ContentWithHeading.vue'
import IndexButtonList from '@/components/IndexButtonList.vue'
import ListTracks from '@/components/ListTracks.vue'
import ModalDialogArtist from '@/components/ModalDialogArtist.vue'
import webapi from '@/webapi'

const dataObject = {
  load: function (to) {
    return Promise.all([
      webapi.library_artist(to.params.artist_id),
      webapi.library_artist_tracks(to.params.artist_id)
    ])
  },

  set: function (vm, response) {
    vm.artist = response[0].data
    vm.tracks = response[1].data.tracks
  }
}

export default {
  name: 'PageArtistTracks',
  components: { ContentWithHeading, ListTracks, IndexButtonList, ModalDialogArtist },

  data () {
    return {
      artist: {},
      tracks: { items: [] },

      show_artist_details_modal: false
    }
  },

  computed: {
    index_list () {
      return [...new Set(this.tracks.items
        .map(track => track.title_sort.charAt(0).toUpperCase()))]
    },

    track_uris () {
      return this.tracks.items.map(a => a.uri).join(',')
    }
  },

  methods: {
    open_artist: function () {
      this.show_details_modal = false
      this.$router.push({ path: '/music/artists/' + this.artist.id })
    },

    play: function () {
      webapi.player_play_uri(this.tracks.items.map(a => a.uri).join(','), true)
    }
  },

  beforeRouteEnter (to, from, next) {
    dataObject.load(to).then((response) => {
      next(vm => dataObject.set(vm, response))
    })
  },
  beforeRouteUpdate (to, from, next) {
    const vm = this
    dataObject.load(to).then((response) => {
      dataObject.set(vm, response)
      next()
    })
  }
}
</script>

<style>
</style>
