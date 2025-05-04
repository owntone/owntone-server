<template>
  <tabs-audiobooks />
  <content-with-heading>
    <template #options>
      <list-index-buttons :indices="artists.indices" />
    </template>
    <template #heading>
      <heading-title :content="heading" />
    </template>
    <template #content>
      <list-artists :items="artists" />
    </template>
  </content-with-heading>
</template>

<script>
import ContentWithHeading from '@/templates/ContentWithHeading.vue'
import { GroupedList } from '@/lib/GroupedList'
import HeadingTitle from '@/components/HeadingTitle.vue'
import ListArtists from '@/components/ListArtists.vue'
import ListIndexButtons from '@/components/ListIndexButtons.vue'
import TabsAudiobooks from '@/components/TabsAudiobooks.vue'
import library from '@/api/library'

export default {
  name: 'PageAudiobooksArtists',
  components: {
    ContentWithHeading,
    HeadingTitle,
    ListIndexButtons,
    ListArtists,
    TabsAudiobooks
  },
  beforeRouteEnter(to, from, next) {
    library.artists('audiobook').then((artists) => {
      next((vm) => {
        vm.artists = new GroupedList(artists, {
          index: { field: 'name_sort', type: String }
        })
      })
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
        subtitle: [{ count: this.artists.count, key: 'data.authors' }],
        title: this.$t('page.audiobooks.artists.title')
      }
    }
  }
}
</script>
