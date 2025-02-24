<template>
  <div>
    <tabs-audiobooks />
    <content-with-heading>
      <template #options>
        <index-button-list :indices="albums.indices" />
      </template>
      <template #heading-left>
        <p class="title is-4" v-text="$t('page.audiobooks.albums.title')" />
        <p
          class="is-size-7 is-uppercase"
          v-text="$t('count.audiobooks', { count: albums.count })"
        />
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
    TabsAudiobooks
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
  }
}
</script>
