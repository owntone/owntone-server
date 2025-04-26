<template>
  <tabs-audiobooks />
  <content-with-heading>
    <template #options>
      <list-index-buttons :indices="albums.indices" />
    </template>
    <template #heading>
      <heading-title :content="heading" />
    </template>
    <template #content>
      <list-albums :items="albums" />
    </template>
  </content-with-heading>
</template>

<script>
import ContentWithHeading from '@/templates/ContentWithHeading.vue'
import { GroupedList } from '@/lib/GroupedList'
import HeadingTitle from '@/components/HeadingTitle.vue'
import ListAlbums from '@/components/ListAlbums.vue'
import ListIndexButtons from '@/components/ListIndexButtons.vue'
import TabsAudiobooks from '@/components/TabsAudiobooks.vue'
import webapi from '@/webapi'

export default {
  name: 'PageAudiobooksAlbums',
  components: {
    ContentWithHeading,
    HeadingTitle,
    ListIndexButtons,
    ListAlbums,
    TabsAudiobooks
  },
  beforeRouteEnter(to, from, next) {
    webapi.library_albums('audiobook').then((albums) => {
      next((vm) => {
        vm.albums = new GroupedList(albums.data, {
          index: { field: 'name_sort', type: String }
        })
      })
    })
  },
  data() {
    return {
      albums: new GroupedList()
    }
  },
  computed: {
    heading() {
      return {
        subtitle: [{ count: this.albums.count, key: 'count.audiobooks' }],
        title: this.$t('page.audiobooks.albums.title')
      }
    }
  }
}
</script>
