<template>
  <content-with-search
    :components="components"
    :expanded="expanded"
    :get-items="getItems"
    :history="history"
    :load="(expanded && searchNext) || null"
    :results="results"
    @search="search"
    @search-library="searchLibrary"
    @search-query="openSearch"
    @search-spotify="search"
    @expand="expand"
  />
</template>

<script setup>
import { computed, onMounted, ref } from 'vue'
import ContentWithSearch from '@/templates/ContentWithSearch.vue'
import ListAlbumsSpotify from '@/components/ListAlbumsSpotify.vue'
import ListArtistsSpotify from '@/components/ListArtistsSpotify.vue'
import ListPlaylistsSpotify from '@/components/ListPlaylistsSpotify.vue'
import ListTracksSpotify from '@/components/ListTracksSpotify.vue'
import services from '@/api/services'
import { useRouter } from 'vue-router'
import { useSearchStore } from '@/stores/search'

const PAGE_SIZE = 3
const PAGE_SIZE_EXPANDED = 50
const SEARCH_TYPES = ['track', 'artist', 'album', 'playlist']

const searchStore = useSearchStore()
const router = useRouter()

const parameters = ref({})
const results = ref(new Map())
const types = ref([...SEARCH_TYPES])

const components = {
  album: ListAlbumsSpotify,
  artist: ListArtistsSpotify,
  playlist: ListPlaylistsSpotify,
  track: ListTracksSpotify
}

const expanded = computed(() => types.value.length === 1)
const history = computed(() =>
  searchStore.history.filter((q) => !q.startsWith('query:'))
)

const getItems = (items) => items.items

const reset = () => {
  results.value.clear()
  types.value.forEach((type) => {
    results.value.set(type, { items: [], total: 0 })
  })
}

const searchItems = async () => {
  const { api, configuration } = await services.spotify.get()
  return api.search(
    searchStore.query,
    types.value,
    configuration.webapi_country,
    parameters.value.limit,
    parameters.value.offset
  )
}

const search = async (
  newTypes = SEARCH_TYPES,
  limit = PAGE_SIZE,
  offset = 0
) => {
  if (!searchStore.query) {
    return
  }
  types.value = newTypes
  parameters.value.limit = limit
  parameters.value.offset = offset
  searchStore.query = searchStore.query.trim()
  reset()
  const data = await searchItems()
  types.value.forEach((type) => {
    results.value.set(type, data[`${type}s`])
  })
  searchStore.add(searchStore.query)
}

const searchLibrary = () => {
  router.push({ name: 'search-library' })
}

const expand = async (type) => {
  await search([type], PAGE_SIZE_EXPANDED)
}

const searchNext = async ({ loaded }) => {
  const items = results.value.get(types.value[0])
  parameters.value.limit = PAGE_SIZE_EXPANDED
  const data = await searchItems()
  const [next] = Object.values(data)
  items.items.push(...next.items)
  parameters.value.offset += next.items.length
  const remaining = Number(next.next && 1000 - parameters.value.offset)
  loaded(remaining, PAGE_SIZE_EXPANDED)
}

const openSearch = (query) => {
  searchStore.query = query
  search()
}

onMounted(() => {
  search()
})
</script>
