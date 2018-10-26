<template>
  <div>
    <tabs-music></tabs-music>

    <template>
      <div class="container" v-if="links.length > 1">
        <div class="columns is-centered">
          <div class="column is-three-quarters">
            <div class="tabs is-centered is-small">
              <ul>
                <tab-idx-nav-item v-for="link in links" :key="link.n" :link="link"></tab-idx-nav-item>
              </ul>
            </div>
          </div>
        </div>
      </div>
    </template>

    <content-with-heading>
      <template slot="heading-left">
        <p class="title is-4">{{ name }}</p>
        <p class="heading">{{ genreAlbums.total }} albums</p>
      </template>
      <template slot="heading-right">
        <a class="button is-small is-dark is-rounded" @click="play">
          <span class="icon">
            <i class="mdi mdi-play"></i>
          </span>
          <span>Play</span>
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
import TabIdxNavItem from '@/components/TabsIdxNav'
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
  components: { ContentWithHeading, TabsMusic, ListItemAlbums, TabIdxNavItem },

  data () {
    return {
      name: '',
      genreAlbums: {},
      links: []
    }
  },

  methods: {
    play: function () {
      webapi.queue_clear().then(() =>
        webapi.queue_add(this.genreAlbums.items.map(a => a.uri).join(',')).then(() =>
          webapi.player_play()
        )
      )
    }
  }
}
</script>

<style>
</style>
