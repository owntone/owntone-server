<template>
  <div>
    <tabs-music></tabs-music>

    <content-with-heading>
      <template slot="heading-left">
        <p class="title is-4">{{ name }}</p>
        <p class="heading">{{ genreAlbums.total }} albums</p>
      </template>
      <template slot="heading-right">
        <a class="button is-small is-dark is-rounded" @click="play">
          <span class="icon"><i class="mdi mdi-shuffle"></i></span> <span>Shuffle</span>
        </a>
      </template>
      <template slot="content">
        <list-item-albums v-for="album in genreAlbums.items" :key="album.id" :album="album" :links="links"></list-item-albums>
      </template>
    </content-with-heading>
  </div>
</template>

<script>
import { LoadDataBeforeEnterMixin } from './mixin'
import ContentWithHeading from '@/templates/ContentWithHeading'
import TabsMusic from '@/components/TabsMusic'
import ListItemAlbums from '@/components/ListItemAlbum'
import webapi from '@/webapi'

const genreData = {
  load: function (to) {
    return webapi.library_genre(to.params.genre)
  },

  set: function (vm, response) {
    vm.name = vm.$route.params.genre
    vm.genreAlbums = response.data.albums
    var li = 0
    var v = null
    var i
    for (i = 0; i < vm.genreAlbums.items.length; i++) {
      var n = vm.genreAlbums.items[i].name_sort.charAt(0).toUpperCase()
      if (n !== v) {
        var obj = {}
        obj.n = n
        obj.a = 'idx_nav_' + li
        vm.links.push(obj)
        li++
        v = n
      }
    }
  }
}

export default {
  name: 'PageGenre',
  mixins: [ LoadDataBeforeEnterMixin(genreData) ],
  components: { ContentWithHeading, TabsMusic, ListItemAlbums },

  data () {
    return {
      name: '',
      genreAlbums: {},
      links: []
    }
  },

  methods: {
    play: function () {
      webapi.player_play_uri(this.genreAlbums.items.map(a => a.uri).join(','), true)
    }
  }
}
</script>

<style>
</style>
