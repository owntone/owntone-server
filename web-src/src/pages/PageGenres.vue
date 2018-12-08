<template>
  <div>
    <tabs-music></tabs-music>

    <content-with-heading>
      <template slot="options">
        <index-button-list :index="index_list"></index-button-list>
      </template>
      <template slot="heading-left">
        <p class="title is-4">Genres</p>
        <p class="heading">{{ genres.total }} genres</p>
      </template>
      <template slot="content">
        <list-item-genre v-for="(genre, index) in genres.items" :key="genre.name" :genre="genre" :anchor="anchor(genre, index)"></list-item-genre>
      </template>
    </content-with-heading>
  </div>
</template>

<script>
import { LoadDataBeforeEnterMixin } from './mixin'
import ContentWithHeading from '@/templates/ContentWithHeading'
import TabsMusic from '@/components/TabsMusic'
import IndexButtonList from '@/components/IndexButtonList'
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
  components: { ContentWithHeading, TabsMusic, IndexButtonList, ListItemGenre },

  data () {
    return {
      genres: { items: [] }
    }
  },

  computed: {
    index_list () {
      return [...new Set(this.genres.items
        .map(genre => genre.name.charAt(0).toUpperCase()))]
    }
  },

  methods: {
    anchor: function (genre, index) {
      return genre.name.charAt(0).toUpperCase()
    }
  }
}
</script>

<style>
</style>
