<template>
  <content-with-heading>
      <template v-slot:options>
        <div class="columns">
          <div class="column">
            <p class="heading" style="margin-bottom: 24px;">Sort by</p>
            <dropdown-menu v-model="sort" :options="sort_options"></dropdown-menu>
          </div>
        </div>
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
      <p class="heading has-text-centered-mobile">{{ artist.album_count }} albums | <a class="has-text-link" @click="open_tracks">{{ artist.track_count }} tracks</a></p>
      <list-albums :albums="albums_list"></list-albums>
      <modal-dialog-artist :show="show_artist_details_modal" :artist="artist" @close="show_artist_details_modal = false" />
    </template>
  </content-with-heading>
</template>

<script>
import ContentWithHeading from '@/templates/ContentWithHeading.vue'
import ListAlbums from '@/components/ListAlbums.vue'
import ModalDialogArtist from '@/components/ModalDialogArtist.vue'
import DropdownMenu from '@/components/DropdownMenu.vue'
import webapi from '@/webapi'
import * as types from '@/store/mutation_types'
import Albums from '@/lib/Albums'

const dataObject = {
  load: function (to) {
    return Promise.all([
      webapi.library_artist(to.params.artist_id),
      webapi.library_artist_albums(to.params.artist_id)
    ])
  },

  set: function (vm, response) {
    vm.artist = response[0].data
    vm.albums = response[1].data
  }
}

export default {
  name: 'PageArtist',
  components: { ContentWithHeading, ListAlbums, ModalDialogArtist, DropdownMenu },

  data () {
    return {
      artist: {},
      albums: { items: [] },

      sort_options: ['Name', 'Release date'],
      show_artist_details_modal: false
    }
  },

  computed: {
    albums_list () {
      return new Albums(this.albums.items, {
        sort: this.sort,
        group: false
      })
    },

    sort: {
      get () {
        return this.$store.state.artist_albums_sort
      },
      set (value) {
        this.$store.commit(types.ARTIST_ALBUMS_SORT, value)
      }
    }
  },

  methods: {
    open_tracks: function () {
      this.$router.push({ path: '/music/artists/' + this.artist.id + '/tracks' })
    },

    play: function () {
      webapi.player_play_uri(this.albums.items.map(a => a.uri).join(','), true)
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
