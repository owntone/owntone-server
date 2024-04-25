<template>
  <div class="fd-page-with-tabs">
    <tabs-music />
    <content-with-heading>
      <template #heading-left>
        <p class="title is-4" v-text="$t('page.music.recently-added.title')" />
      </template>
      <template #content>
        <list-albums :items="recently_added" />
      </template>
    </content-with-heading>
  </div>
</template>

<script>
import ContentWithHeading from '@/templates/ContentWithHeading.vue'
import { GroupedList } from '@/lib/GroupedList'
import ListAlbums from '@/components/ListAlbums.vue'
import TabsMusic from '@/components/TabsMusic.vue'
import store from '@/store'
import webapi from '@/webapi'

const dataObject = {
  load(to) {
    const limit = store.getters.setting_recently_added_limit
    return webapi.search({
      expression:
        'media_kind is music having track_count > 3 order by time_added desc',
      limit,
      type: 'album'
    })
  },

  set(vm, response) {
    vm.recently_added = new GroupedList(response.data.albums, {
      criteria: [{ field: 'time_added', order: -1, type: Date }],
      index: { field: 'time_added', type: Date }
    })
  }
}

export default {
  name: 'PageMusicRecentlyAdded',
  components: { ContentWithHeading, ListAlbums, TabsMusic },

  beforeRouteEnter(to, from, next) {
    dataObject.load(to).then((response) => {
      next((vm) => dataObject.set(vm, response))
    })
  },

  data() {
    return {
      recently_added: new GroupedList()
    }
  }
}
</script>

<style></style>
