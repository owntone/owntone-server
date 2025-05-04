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
import library from '@/api/library'

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
    library.albums('audiobook').then((albums) => {
      next((vm) => {
        vm.albums = new GroupedList(albums, {
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
        subtitle: [{ count: this.albums.count, key: 'data.audiobooks' }],
        title: this.$t('page.audiobooks.albums.title')
      }
    }
  }
}
</script>
