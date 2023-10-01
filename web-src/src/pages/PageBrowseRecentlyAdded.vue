<template>
  <div class="fd-page-with-tabs">
    <tabs-music />
    <content-with-heading>
      <template #heading-left>
        <p class="title is-4" v-text="$t('page.browse.recently-added.title')" />
        <p class="heading" v-text="$t('page.browse.recently-added.albums')" />
      </template>
      <template #content>
        <list-albums :albums="recently_added" />
      </template>
    </content-with-heading>
  </div>
</template>

<script>
import ContentWithHeading from '@/templates/ContentWithHeading.vue'
import { GroupByList, byDateSinceToday } from '@/lib/GroupByList'
import ListAlbums from '@/components/ListAlbums.vue'
import store from '@/store'
import TabsMusic from '@/components/TabsMusic.vue'
import webapi from '@/webapi'

const dataObject = {
  load(to) {
    const limit = store.getters.settings_option_recently_added_limit
    return webapi.search({
      type: 'album',
      expression:
        'media_kind is music having track_count > 3 order by time_added desc',
      limit: limit
    })
  },

  set(vm, response) {
    vm.recently_added = new GroupByList(response.data.albums)
    vm.recently_added.group(
      byDateSinceToday('time_added', {
        direction: 'desc'
      })
    )
  }
}

export default {
  name: 'PageBrowseType',
  components: { ContentWithHeading, TabsMusic, ListAlbums },

  beforeRouteEnter(to, from, next) {
    dataObject.load(to).then((response) => {
      next((vm) => dataObject.set(vm, response))
    })
  },

  beforeRouteUpdate(to, from, next) {
    if (!this.recently_added.isEmpty()) {
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
      recently_added: new GroupByList()
    }
  }
}
</script>

<style></style>
