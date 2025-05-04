<template>
  <tabs-music />
  <content-with-heading>
    <template #heading>
      <heading-title
        :content="{ title: $t('page.music.recently-added.title') }"
      />
    </template>
    <template #content>
      <list-albums :items="albums" />
    </template>
    <template #footer>
      <router-link
        class="button is-small is-rounded"
        :to="{ name: 'music-recently-added' }"
      >
        {{ $t('actions.show-more') }}
      </router-link>
    </template>
  </content-with-heading>
  <content-with-heading>
    <template #heading>
      <heading-title
        :content="{ title: $t('page.music.recently-played.title') }"
      />
    </template>
    <template #content>
      <list-tracks :items="tracks" />
    </template>
    <template #footer>
      <router-link
        class="button is-small is-rounded"
        :to="{ name: 'music-recently-played' }"
      >
        {{ $t('actions.show-more') }}
      </router-link>
    </template>
  </content-with-heading>
</template>

<script>
import ContentWithHeading from '@/templates/ContentWithHeading.vue'
import { GroupedList } from '@/lib/GroupedList'
import HeadingTitle from '@/components/HeadingTitle.vue'
import ListAlbums from '@/components/ListAlbums.vue'
import ListTracks from '@/components/ListTracks.vue'
import TabsMusic from '@/components/TabsMusic.vue'
import library from '@/api/library'

export default {
  name: 'PageMusic',
  components: {
    ContentWithHeading,
    HeadingTitle,
    ListAlbums,
    ListTracks,
    TabsMusic
  },
  beforeRouteEnter(to, from, next) {
    Promise.all([
      library.search({
        expression:
          'time_added after 8 weeks ago and media_kind is music having track_count > 3 order by time_added desc',
        limit: 3,
        type: 'album'
      }),
      library.search({
        expression:
          'time_played after 8 weeks ago and media_kind is music order by time_played desc',
        limit: 3,
        type: 'track'
      })
    ]).then(([{ albums }, { tracks }]) => {
      next((vm) => {
        vm.albums = new GroupedList(albums)
        vm.tracks = new GroupedList(tracks)
      })
    })
  },
  data() {
    return {
      albums: [],
      tracks: null
    }
  }
}
</script>
