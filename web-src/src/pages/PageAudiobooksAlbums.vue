<template>
  <div class="fd-page-with-tabs">
    <tabs-audiobooks />
    <content-with-heading>
      <template #options>
        <index-button-list :index="albums.indexList" />
      </template>
      <template #heading-left>
        <p class="title is-4" v-text="$t('page.audiobooks.albums.title')" />
        <p
          class="heading"
          v-text="$t('page.audiobooks.albums.count', { count: albums.count })"
        />
      </template>
      <template #content>
        <list-albums :albums="albums" />
      </template>
    </content-with-heading>
  </div>
</template>

<script>
import ContentWithHeading from '@/templates/ContentWithHeading.vue'
import { GroupByList, byName } from '@/lib/GroupByList'
import IndexButtonList from '@/components/IndexButtonList.vue'
import ListAlbums from '@/components/ListAlbums.vue'
import TabsAudiobooks from '@/components/TabsAudiobooks.vue'
import webapi from '@/webapi'

const dataObject = {
  load(to) {
    return webapi.library_albums('audiobook')
  },

  set(vm, response) {
    vm.albums = new GroupByList(response.data)
    vm.albums.group(byName('name_sort', true))
  }
}

export default {
  name: 'PageAudiobooksAlbums',
  components: {
    TabsAudiobooks,
    ContentWithHeading,
    IndexButtonList,
    ListAlbums
  },

  beforeRouteEnter(to, from, next) {
    dataObject.load(to).then((response) => {
      next((vm) => dataObject.set(vm, response))
    })
  },

  beforeRouteUpdate(to, from, next) {
    if (!this.albums.isEmpty()) {
      next()
      return
    }
    const vm = this
    dataObject.load(to).then((response) => {
      dataObject.set(vm, response)
      next()
    })
  },

  data() {
    return {
      albums: new GroupByList()
    }
  }
}
</script>

<style></style>
