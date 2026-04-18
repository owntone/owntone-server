<template>
  <content-with-search
    :components="components"
    :expanded="expanded"
    :get-items="getItems"
    :history="history"
    :results="results"
    @search="search"
    @search-library="search"
    @search-query="openSearch"
    @search-spotify="searchSpotify"
    @expand="expand"
  >
    <template #help>
      <i18n-t
        tag="p"
        class="help has-text-centered"
        keypath="page.search.help"
        scope="global"
      >
        <template #query>
          <code>query:</code>
        </template>
        <template #help>
          <a
            href="https://owntone.github.io/owntone-server/smart-playlists/"
            target="_blank"
            v-text="$t('page.search.expression')"
          />
        </template>
      </i18n-t>
    </template>
  </content-with-search>
</template>

<script setup>
import { computed, onMounted, ref } from 'vue'
import ContentWithSearch from '@/templates/ContentWithSearch.vue'
import { GroupedList } from '@/lib/GroupedList'
import ListAlbums from '@/components/ListAlbums.vue'
import ListArtists from '@/components/ListArtists.vue'
import ListComposers from '@/components/ListComposers.vue'
import ListPlaylists from '@/components/ListPlaylists.vue'
import ListTracks from '@/components/ListTracks.vue'
import library from '@/api/library'
import { useRouter } from 'vue-router'
import { useSearchStore } from '@/stores/search'

const PAGE_SIZE = 3
const SEARCH_TYPES = [
  'track',
  'artist',
  'album',
  'composer',
  'playlist',
  'audiobook',
  'podcast'
]

const router = useRouter()
const searchStore = useSearchStore()

const components = {
  album: ListAlbums,
  audiobook: ListAlbums,
  artist: ListArtists,
  composer: ListComposers,
  playlist: ListPlaylists,
  podcast: ListAlbums,
  track: ListTracks
}

const limit = ref(PAGE_SIZE)
const results = ref(new Map())
const types = ref(SEARCH_TYPES)

const expanded = computed(() => types.value.length === 1)

const history = computed(() => searchStore.history)

const getItems = (items) => items

const reset = () => {
  results.value.clear()
  types.value.forEach((type) => {
    results.value.set(type, new GroupedList())
  })
}

const searchItems = async (type) => {
  const music = type !== 'audiobook' && type !== 'podcast'
  const kind = (music && 'music') || type
  const parameters = {
    limit: limit.value,
    type: (music && type) || 'album'
  }
  if (searchStore.query.startsWith('query:')) {
    parameters.expression = `(${searchStore.query.replace(/^query:/u, '').trim()}) and media_kind is ${kind}`
  } else if (music) {
    parameters.query = searchStore.query
    parameters.media_kind = kind
  } else {
    parameters.expression = `(album includes "${searchStore.query}" or artist includes "${searchStore.query}") and media_kind is ${kind}`
  }
  const data = await library.search(parameters)
  results.value.set(type, new GroupedList(data[`${parameters.type}s`]))
}

const search = (typesArg = SEARCH_TYPES, limitArg = PAGE_SIZE) => {
  if (searchStore.query) {
    types.value = typesArg
    limit.value = limitArg
    searchStore.query = searchStore.query.trim()
    reset()
    types.value.forEach((type) => {
      searchItems(type)
    })
    searchStore.add(searchStore.query)
  }
}

const openSearch = (query) => {
  searchStore.query = query
  search()
}

const expand = (type) => {
  search([type], -1)
}

const searchSpotify = () => {
  router.push({ name: 'search-spotify' })
}

onMounted(() => {
  search()
})
</script>
