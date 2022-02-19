<template>
  <div>
    <content-with-heading>
      <template v-slot:options>
        <index-button-list :index="index_list"></index-button-list>
      </template>
      <template v-slot:heading-left>
        <p class="title is-4">{{ name }}</p>
      </template>
      <template v-slot:heading-right>
        <div class="buttons is-centered">
          <a class="button is-small is-light is-rounded" @click="show_genre_details_modal = true">
            <span class="icon"><i class="mdi mdi-dots-horizontal mdi-18px"></i></span>
          </a>
          <a class="button is-small is-dark is-rounded" @click="play">
            <span class="icon"><i class="mdi mdi-shuffle"></i></span> <span>Shuffle</span>
          </a>
        </div>
      </template>
      <template v-slot:content>
        <p class="heading has-text-centered-mobile">{{ genre_albums.total }} albums | <a class="has-text-link" @click="open_tracks">tracks</a></p>
        <list-albums :albums="genre_albums.items"></list-albums>
        <modal-dialog-genre :show="show_genre_details_modal" :genre="{ 'name': name }" @close="show_genre_details_modal = false" />
      </template>
    </content-with-heading>
  </div>
</template>

<script>
import ContentWithHeading from '@/templates/ContentWithHeading.vue'
import IndexButtonList from '@/components/IndexButtonList.vue'
import ListAlbums from '@/components/ListAlbums.vue'
import ModalDialogGenre from '@/components/ModalDialogGenre.vue'
import webapi from '@/webapi'

const dataObject = {
  load: function (to) {
    return webapi.library_genre(to.params.genre)
  },

  set: function (vm, response) {
    vm.name = vm.$route.params.genre
    vm.genre_albums = response.data.albums
  }
}

export default {
  name: 'PageGenre',
  components: { ContentWithHeading, IndexButtonList, ListAlbums, ModalDialogGenre },

  data () {
    return {
      name: '',
      genre_albums: { items: [] },

      show_genre_details_modal: false
    }
  },

  computed: {
    index_list () {
      return [...new Set(this.genre_albums.items
        .map(album => album.name.charAt(0).toUpperCase()))]
    }
  },

  methods: {
    open_tracks: function () {
      this.show_details_modal = false
      this.$router.push({ name: 'GenreTracks', params: { genre: this.name } })
    },

    play: function () {
      webapi.player_play_expression('genre is "' + this.name + '" and media_kind is music', true)
    },

    open_dialog: function (album) {
      this.selected_album = album
      this.show_details_modal = true
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
