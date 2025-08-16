<template>
  <content-with-lists :results="results" :types="types" />
</template>

<script>
import ContentWithLists from '@/templates/ContentWithLists.vue'
import { GroupedList } from '@/lib/GroupedList'
import ListAlbums from '@/components/ListAlbums.vue'
import ListTracks from '@/components/ListTracks.vue'
import library from '@/api/library'
import { useSettingsStore } from '@/stores/settings'

const PAGE_SIZE = 3
const PAGE_SIZE_EXPANDED = 50

export default {
  name: 'PageMusic',
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
          title: 'page.music.recently-added.title',
          component: ListAlbums,
          handler: this.recentlyAddedAlbums
        },
        track: {
          title: 'page.music.recently-played.title',
          component: ListTracks,
          handler: this.recentlyPlayedTracks
        }
      }
    }
  },
  methods: {
    recentlyAddedAlbums(limit, expanded, loaded) {
      library
        .search({
          expression:
            'media_kind is music having track_count > 3 order by time_added desc',
          limit: limit || useSettingsStore().recentlyAddedLimit,
          type: 'album'
        })
        .then(({ albums }) => {
          this.storeResults({
            type: 'album',
            items: new GroupedList(albums, {
              criteria: [{ field: 'time_added', order: -1, type: Date }],
              index: { field: 'time_added', type: Date }
            }),
            expanded,
            loaded
          })
        })
    },
    recentlyPlayedTracks(limit, expanded, loaded) {
      library
        .search({
          expression: 'media_kind is music order by time_played desc',
          limit,
          type: 'track'
        })
        .then(({ tracks }) => {
          this.storeResults({
            type: 'track',
            items: new GroupedList(tracks, {
              criteria: [{ field: 'time_played', order: -1, type: Date }],
              index: { field: 'time_played', type: Date }
            }),
            expanded,
            loaded
          })
        })
    },
    storeResults({ type, items, expanded, loaded }) {
      if (loaded) {
        const current = this.results.get(type) || []
        const updated = [...current, ...items.items]
        this.results.clear()
        this.results.set(type, new GroupedList(updated))
        loaded(items.total - updated.length, PAGE_SIZE_EXPANDED)
      } else {
        if (expanded) {
          this.results.clear()
        }
        this.results.set(type, items)
      }
    }
  }
}
</script>
