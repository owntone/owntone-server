<template>
  <div>
    <tabs-audiobooks />
    <content-with-heading>
      <template #options>
        <index-button-list :indices="artists.indices" />
      </template>
      <template #heading-left>
        <div class="title is-4" v-text="$t('page.audiobooks.artists.title')" />
        <div
          class="is-size-7 is-uppercase"
          v-text="$t('page.audiobooks.artists.count', { count: artists.count })"
        />
      </template>
      <template #content>
        <list-artists :items="artists" />
      </template>
    </content-with-heading>
  </div>
</template>

<script>
import ContentWithHeading from '@/templates/ContentWithHeading.vue'
import { GroupedList } from '@/lib/GroupedList'
import IndexButtonList from '@/components/IndexButtonList.vue'
import ListArtists from '@/components/ListArtists.vue'
import TabsAudiobooks from '@/components/TabsAudiobooks.vue'
import webapi from '@/webapi'

const dataObject = {
  load(to) {
    return webapi.library_artists('audiobook')
  },

  set(vm, response) {
    vm.artists = new GroupedList(response.data, {
      index: { field: 'name_sort', type: String }
    })
  }
}

export default {
  name: 'PageAudiobooksArtists',
  components: {
    ContentWithHeading,
    IndexButtonList,
    ListArtists,
    TabsAudiobooks
  },

  beforeRouteEnter(to, from, next) {
    dataObject.load(to).then((response) => {
      next((vm) => dataObject.set(vm, response))
    })
  },

  data() {
    return {
      artists: new GroupedList()
    }
  }
}
</script>
