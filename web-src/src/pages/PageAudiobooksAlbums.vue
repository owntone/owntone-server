<template>
  <div>
    <tabs-audiobooks />
    <content-with-heading>
      <template #options>
        <index-button-list :indices="albums.indices" />
      </template>
      <template #heading-left>
        <heading-title :content="heading" />
      </template>
      <template #content>
        <list-albums :items="albums" />
      </template>
    </content-with-heading>
  </div>
</template>

<script>
import ContentWithHeading from '@/templates/ContentWithHeading.vue'
import { GroupedList } from '@/lib/GroupedList'
import HeadingTitle from '@/components/HeadingTitle.vue'
import IndexButtonList from '@/components/IndexButtonList.vue'
import ListAlbums from '@/components/ListAlbums.vue'
import TabsAudiobooks from '@/components/TabsAudiobooks.vue'
import webapi from '@/webapi'

const dataObject = {
  load(to) {
    return webapi.library_albums('audiobook')
  },
  set(vm, response) {
    vm.albums = new GroupedList(response.data, {
      index: { field: 'name_sort', type: String }
    })
  }
}

export default {
  name: 'PageAudiobooksAlbums',
  components: {
    ContentWithHeading,
    IndexButtonList,
    ListAlbums,
    TabsAudiobooks,
    HeadingTitle
  },
  beforeRouteEnter(to, from, next) {
    dataObject.load(to).then((response) => {
      next((vm) => dataObject.set(vm, response))
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
        title: this.$t('page.audiobooks.albums.title'),
        subtitle: [{ key: 'count.audiobooks', count: this.albums.count }]
      }
    }
  }
}
</script>
