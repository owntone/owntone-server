<template>
  <div class="fd-page-with-tabs">
    <tabs-music></tabs-music>

    <content-with-heading>
      <template v-slot:options>
        <index-button-list :index="index_list"></index-button-list>
      </template>
      <template v-slot:heading-left>
        <p class="title is-4">Genres</p>
        <p class="heading">{{ genres.total }} genres</p>
      </template>
      <template v-slot:content>
        <list-item-genre v-for="genre in genres.items" :key="genre.name" :genre="genre" @click="open_genre(genre)">
          <template v-slot:actions>
            <a @click.prevent.stop="open_dialog(genre)">
              <span class="icon has-text-dark"><i class="mdi mdi-dots-vertical mdi-18px"></i></span>
            </a>
          </template>
        </list-item-genre>
        <modal-dialog-genre :show="show_details_modal" :genre="selected_genre" @close="show_details_modal = false" />
      </template>
    </content-with-heading>
  </div>
</template>

<script>
import ContentWithHeading from '@/templates/ContentWithHeading.vue'
import TabsMusic from '@/components/TabsMusic.vue'
import IndexButtonList from '@/components/IndexButtonList.vue'
import ListItemGenre from '@/components/ListItemGenre.vue'
import ModalDialogGenre from '@/components/ModalDialogGenre.vue'
import webapi from '@/webapi'

const dataObject = {
  load: function (to) {
    return webapi.library_genres()
  },

  set: function (vm, response) {
    vm.genres = response.data
  }
}

export default {
  name: 'PageGenres',
  components: { ContentWithHeading, TabsMusic, IndexButtonList, ListItemGenre, ModalDialogGenre },

  data () {
    return {
      genres: { items: [] },

      show_details_modal: false,
      selected_genre: {}
    }
  },

  computed: {
    index_list () {
      return [...new Set(this.genres.items
        .map(genre => genre.name.charAt(0).toUpperCase()))]
    }
  },

  methods: {
    open_genre: function (genre) {
      this.$router.push({ name: 'Genre', params: { genre: genre.name } })
    },

    open_dialog: function (genre) {
      this.selected_genre = genre
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
