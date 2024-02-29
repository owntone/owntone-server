<template>
  <div class="fd-page-with-tabs">
    <tabs-music />
    <content-with-heading>
      <template #heading-left>
        <p class="title is-4" v-text="$t('page.music.recently-added.title')" />
      </template>
      <template #content>
        <list-albums :albums="recently_added" />
      </template>
    </content-with-heading>
  </div>
</template>

<script>
import { GroupedList, byDateSinceToday } from '@/lib/GroupedList'
import ContentWithHeading from '@/templates/ContentWithHeading.vue'
import ListAlbums from '@/components/ListAlbums.vue'
import TabsMusic from '@/components/TabsMusic.vue'
import store from '@/store'
import webapi from '@/webapi'

const dataObject = {
  load(to) {
    const limit = store.getters.settings_option_recently_added_limit
    return webapi.search({
      type: 'album',
      expression:
        'media_kind is music having track_count > 3 order by time_added desc',
      limit
    })
  },

  set(vm, response) {
    vm.recently_added = new GroupedList(response.data.albums)
    vm.recently_added.group(
      byDateSinceToday('time_added', {
        direction: 'desc'
      })
    )
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

  beforeRouteUpdate(to, from, next) {
    const vm = this
    dataObject.load(to).then((response) => {
      dataObject.set(vm, response)
      next()
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
