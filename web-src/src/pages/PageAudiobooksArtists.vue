<template>
  <div>
    <tabs-audiobooks />
    <content-with-heading>
      <template #options>
        <index-button-list :indices="artists.indices" />
      </template>
      <template #heading-left>
        <heading-title :content="heading" />
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
import HeadingTitle from '@/components/HeadingTitle.vue'
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
    HeadingTitle,
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
  },
  computed: {
    heading() {
      return {
        subtitle: [{ count: this.artists.count, key: 'count.authors' }],
        title: this.$t('page.audiobooks.artists.title')
      }
    }
  }
}
</script>
