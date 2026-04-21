<template>
  <tabs-music />
  <content-with-heading v-if="albums">
    <template #heading>
      <pane-title :content="{ title: $t('page.spotify.music.new-releases') }" />
    </template>
    <template #content>
      <list-albums-spotify :items="albums" />
    </template>
    <template #footer>
      <router-link
        :to="{ name: 'music-spotify-new-releases' }"
        class="button is-small is-rounded"
      >
        {{ $t('actions.show-more') }}
      </router-link>
    </template>
  </content-with-heading>
  <content-with-heading v-if="playlists">
    <template #heading>
      <pane-title
        :content="{ title: $t('page.spotify.music.featured-playlists') }"
      />
    </template>
    <template #content>
      <list-playlists-spotify :items="playlists" />
    </template>
    <template #footer>
      <router-link
        :to="{ name: 'music-spotify-featured-playlists' }"
        class="button is-small is-rounded"
      >
        {{ $t('actions.show-more') }}
      </router-link>
    </template>
  </content-with-heading>
  <content-with-heading v-if="artists">
    <template #heading>
      <pane-title
        :content="{ title: $t('page.spotify.music.followed-artists') }"
      />
    </template>
    <template #content>
      <list-artists-spotify :items="artists" />
    </template>
    <template #footer>
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
import { onMounted, ref } from 'vue'
import ContentWithHeading from '@/templates/ContentWithHeading.vue'
import ListAlbumsSpotify from '@/components/ListAlbumsSpotify.vue'
import ListArtistsSpotify from '@/components/ListArtistsSpotify.vue'
import ListPlaylistsSpotify from '@/components/ListPlaylistsSpotify.vue'
import PaneTitle from '@/components/PaneTitle.vue'
import TabsMusic from '@/components/TabsMusic.vue'
import services from '@/api/services'

const PAGE_SIZE = 3

const albums = ref([])
const artists = ref([])
const playlists = ref([])

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
