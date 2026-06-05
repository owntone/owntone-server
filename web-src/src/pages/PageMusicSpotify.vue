<template>
  <tabs-music />
  <content-with-heading>
    <template #heading>
      <pane-title :content="headingNewReleases" />
    </template>
    <template #content>
      <list-albums-spotify :items="albums" />
    </template>
    <template v-if="albums.length" #footer>
      <router-link
        :to="{ name: 'music-spotify-new-releases' }"
        class="button is-small is-rounded"
      >
        {{ $t('actions.show-more') }}
      </router-link>
    </template>
  </content-with-heading>
  <content-with-heading>
    <template #heading>
      <pane-title :content="headingFeaturedPlaylists" />
    </template>
    <template #content>
      <list-playlists-spotify :items="playlists" />
    </template>
    <template v-if="playlists.length" #footer>
      <router-link
        :to="{ name: 'music-spotify-featured-playlists' }"
        class="button is-small is-rounded"
      >
        {{ $t('actions.show-more') }}
      </router-link>
    </template>
  </content-with-heading>
  <content-with-heading>
    <template #heading>
      <pane-title :content="headingFollowedArtists" />
    </template>
    <template #content>
      <list-artists-spotify :items="artists" />
    </template>
    <template v-if="artists.length" #footer>
      <router-link
        :to="{ name: 'music-spotify-followed-artists' }"
        class="button is-small is-rounded"
      >
        {{ $t('actions.show-more') }}
      </router-link>
    </template>
  </content-with-heading>
</template>

<script setup>
import { computed, onMounted, ref } from 'vue'
import ContentWithHeading from '@/templates/ContentWithHeading.vue'
import ListAlbumsSpotify from '@/components/ListAlbumsSpotify.vue'
import ListArtistsSpotify from '@/components/ListArtistsSpotify.vue'
import ListPlaylistsSpotify from '@/components/ListPlaylistsSpotify.vue'
import PaneTitle from '@/components/PaneTitle.vue'
import TabsMusic from '@/components/TabsMusic.vue'
import services from '@/api/services'
import { useI18n } from 'vue-i18n'

const PAGE_SIZE = 3

const albums = ref([])
const artists = ref([])
const playlists = ref([])

const { t } = useI18n()

const headingFeaturedPlaylists = computed(() => ({
  subtitle: [{ count: playlists.value.length, key: 'data.playlists' }],
  title: t('page.spotify.music.featured-playlists')
}))

const headingFollowedArtists = computed(() => ({
  subtitle: [{ count: artists.value.length, key: 'data.artists' }],
  title: t('page.spotify.music.followed-artists')
}))

const headingNewReleases = computed(() => ({
  subtitle: [{ count: albums.value.length, key: 'data.albums' }],
  title: t('page.spotify.music.new-releases')
}))

onMounted(async () => {
  const { api, configuration } = await services.spotify.get()
  const [newReleases, followedArtists, featuredPlaylists] = await Promise.all([
    api.browse.getNewReleases(configuration.webapi_country, PAGE_SIZE),
    api.currentUser.followedArtists(null, PAGE_SIZE),
    api.browse.getFeaturedPlaylists(
      configuration.webapi_country,
      null,
      null,
      PAGE_SIZE
    )
  ])
  albums.value = newReleases.albums.items
  artists.value = followedArtists?.artists.items
  playlists.value = featuredPlaylists.playlists.items
})
</script>
