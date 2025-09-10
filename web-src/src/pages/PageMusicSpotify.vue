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

<script>
import ContentWithHeading from '@/templates/ContentWithHeading.vue'
import ListAlbumsSpotify from '@/components/ListAlbumsSpotify.vue'
import ListArtistsSpotify from '@/components/ListArtistsSpotify.vue'
import ListPlaylistsSpotify from '@/components/ListPlaylistsSpotify.vue'
import PaneTitle from '@/components/PaneTitle.vue'
import TabsMusic from '@/components/TabsMusic.vue'
import services from '@/api/services'

export default {
  name: 'PageMusicSpotify',
  components: {
    ContentWithHeading,
    ListAlbumsSpotify,
    ListArtistsSpotify,
    ListPlaylistsSpotify,
    PaneTitle,
    TabsMusic
  },
  beforeRouteEnter(to, from, next) {
    services.spotify().then(({ api, configuration }) => {
      Promise.all([
        api.browse.getNewReleases(configuration.webapi_country, 3),
        api.currentUser.followedArtists(null, 3),
        api.browse.getFeaturedPlaylists(
          configuration.webapi_country,
          null,
          null,
          3
        )
      ]).then(([newReleases, followedArtists, featuredPlaylists]) => {
        next((vm) => {
          vm.albums = newReleases.albums.items
          vm.artists = followedArtists?.artists.items
          vm.playlists = featuredPlaylists.playlists.items
        })
      })
    })
  },
  data() {
    return { albums: [], artists: [], playlists: [] }
  }
}
</script>
