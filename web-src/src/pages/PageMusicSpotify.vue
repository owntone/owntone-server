<template>
  <content-with-lists :results="results" :types="types" />
</template>

<script>
import ContentWithLists from '@/templates/ContentWithLists.vue'
import ListAlbumsSpotify from '@/components/ListAlbumsSpotify.vue'
import ListPlaylistsSpotify from '@/components/ListPlaylistsSpotify.vue'
import services from '@/api/services'

const PAGE_SIZE = 3
const PAGE_SIZE_EXPANDED = 50

export default {
  name: 'PageMusicSpotify',
  components: { ContentWithLists },
  beforeRouteEnter(to, from, next) {
    next((vm) => {
      Object.values(vm.types).forEach(({ handler }) => {
        handler(PAGE_SIZE)
      })
    })
  },
  data() {
    return {
      results: new Map()
    }
  },
  computed: {
    types() {
      return {
        album: {
          title: 'page.spotify.music.new-releases',
          component: ListAlbumsSpotify,
          handler: this.newReleases
        },
        playlist: {
          title: 'page.spotify.music.featured-playlists',
          component: ListPlaylistsSpotify,
          handler: this.featuredPlaylists
        }
      }
    }
  },
  methods: {
    featuredPlaylists(limit, expanded, loaded) {
      services.spotify().then(({ api, configuration }) => {
        api.browse
          .getFeaturedPlaylists(configuration.webapi_country, null, null, limit)
          .then(({ playlists }) => {
            this.storeResults({
              type: 'playlist',
              items: playlists,
              expanded,
              loaded
            })
          })
      })
    },
    newReleases(limit, expanded, loaded) {
      services.spotify().then(({ api, configuration }) => {
        api.browse
          .getNewReleases(configuration.webapi_country, limit)
          .then(({ albums }) => {
            this.storeResults({
              type: 'album',
              items: albums,
              expanded,
              loaded
            })
          })
      })
    },
    storeResults({ type, items, expanded = false, loaded = null }) {
      if (loaded) {
        const current = this.results.get(type) || []
        const updated = [...current, ...items.items]
        this.results.clear()
        this.results.set(type, updated)
        loaded(items.total - updated.length, PAGE_SIZE_EXPANDED)
      } else {
        if (expanded) {
          this.results.clear()
        }
        this.results.set(type, items.items)
      }
    }
  }
}
</script>
