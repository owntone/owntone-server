<template>
  <div>
    <tabs-music />
    <content-with-heading>
      <template #heading-left>
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
      <template #heading-left>
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
  </div>
</template>

<script>
import ContentWithHeading from '@/templates/ContentWithHeading.vue'
import { GroupedList } from '@/lib/GroupedList'
import HeadingTitle from '@/components/HeadingTitle.vue'
import ListAlbums from '@/components/ListAlbums.vue'
import ListTracks from '@/components/ListTracks.vue'
import TabsMusic from '@/components/TabsMusic.vue'
import webapi from '@/webapi'

const dataObject = {
  load(to) {
    return Promise.all([
      webapi.search({
        expression:
          'time_added after 8 weeks ago and media_kind is music having track_count > 3 order by time_added desc',
        limit: 3,
        type: 'album'
      }),
      webapi.search({
        expression:
          'time_played after 8 weeks ago and media_kind is music order by time_played desc',
        limit: 3,
        type: 'track'
      })
    ])
  },
  set(vm, response) {
    vm.albums = new GroupedList(response[0].data.albums)
    vm.tracks = new GroupedList(response[1].data.tracks)
  }
}

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
    dataObject.load(to).then((response) => {
      next((vm) => dataObject.set(vm, response))
    })
  },
  data() {
    return {
      albums: [],
      tracks: { items: [] },
      selected_track: {}
    }
  }
}
</script>
