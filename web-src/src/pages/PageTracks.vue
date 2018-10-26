<template>
  <div>
    <content-with-heading>
      <template slot="heading-left">
        <a class="title is-4 has-text-link has-text-weight-normal" @click="open_artist">{{ artist }}</a>
        <p class="heading">{{ tracks.total }} tracks</p>
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
        <div class="columns is-centered">
          <div class="column is-three-quarters">
            <div class="tabs is-centered is-small">
              <ul>
                <tab-idx-nav-item v-for="link in links" :key="link.n" :link="link"></tab-idx-nav-item>
              </ul>
            </div>
          </div>
        </div>
        <list-item-track v-for="(track, index) in tracks.items" :key="track.id" :track="track" :position="index" :context_uri="track.uri" :links="links"></list-item-track>
      </template>
    </content-with-heading>
  </div>
</template>

<script>
import { LoadDataBeforeEnterMixin } from './mixin'
import ContentWithHeading from '@/templates/ContentWithHeading'
import ListItemTrack from '@/components/ListItemTrack'
import TabIdxNavItem from '@/components/TabsIdxNav'
import webapi from '@/webapi'

const tracksData = {
  load: function (to) {
    return webapi.library_artist_tracks(to.params.artist)
  },

  set: function (vm, response) {
    vm.artist_id = vm.$route.params.artist
    vm.tracks = response.data.tracks

    var li = 0
    var v = null
    var i
    for (i = 0; i < vm.tracks.items.length; i++) {
      if (i === 0) {
        vm.artist = vm.tracks.items[0].artist
      }
      var n = vm.tracks.items[i].title.charAt(0).toUpperCase()
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
  name: 'PageTracks',
  mixins: [ LoadDataBeforeEnterMixin(tracksData) ],
  components: { ContentWithHeading, ListItemTrack, TabIdxNavItem },

  data () {
    return {
      tracks: {},
      artist: '',
      artist_id: 0,
      links: []
    }
  },

  methods: {
    open_artist: function () {
      this.show_details_modal = false
      this.$router.push({ path: '/music/artists/' + this.artist_id })
    },

    play: function () {
      webapi.queue_clear().then(() =>
        webapi.queue_add(this.tracks.items.map(a => a.uri).join(',')).then(() =>
          webapi.player_play()
        )
      )
    }
  }
}
</script>

<style>
</style>
