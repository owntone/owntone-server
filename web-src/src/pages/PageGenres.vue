<template>
  <div>
    <tabs-music></tabs-music>

    <content-with-heading>
      <template slot="heading-left">
        <p class="title is-4">Genres</p>
        <p class="heading">{{ genres.total }} genres</p>
      </template>
      <template slot="content">
        <list-item-genre v-for="genre in genres.items" :key="genre.name" :genre="genre"></list-item-genre>
      </template>
    </content-with-heading>
  </div>
</template>

<script>
import { LoadDataBeforeEnterMixin } from './mixin'
import ContentWithHeading from '@/templates/ContentWithHeading'
import TabsMusic from '@/components/TabsMusic'
import ListItemGenre from '@/components/ListItemGenre'
import webapi from '@/webapi'

const genresData = {
  load: function (to) {
    return webapi.library_genres()
  },

  set: function (vm, response) {
    vm.genres = response.data
  }
}

export default {
  name: 'PageGenres',
  mixins: [ LoadDataBeforeEnterMixin(genresData) ],
  components: { ContentWithHeading, TabsMusic, ListItemGenre },

  data () {
    return {
      genres: {}
    }
  },

  methods: {
  }
}
</script>

<style>
</style>
