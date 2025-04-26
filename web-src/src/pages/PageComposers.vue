<template>
  <tabs-music />
  <content-with-heading>
    <template #options>
      <list-index-buttons :indices="composers.indices" />
    </template>
    <template #heading>
      <heading-title :content="heading" />
    </template>
    <template #content>
      <list-composers :items="composers" />
    </template>
  </content-with-heading>
</template>

<script>
import ContentWithHeading from '@/templates/ContentWithHeading.vue'
import { GroupedList } from '@/lib/GroupedList'
import HeadingTitle from '@/components/HeadingTitle.vue'
import ListComposers from '@/components/ListComposers.vue'
import ListIndexButtons from '@/components/ListIndexButtons.vue'
import TabsMusic from '@/components/TabsMusic.vue'
import webapi from '@/webapi'

export default {
  name: 'PageComposers',
  components: {
    ContentWithHeading,
    HeadingTitle,
    ListIndexButtons,
    ListComposers,
    TabsMusic
  },
  beforeRouteEnter(to, from, next) {
    webapi.library_composers('music').then((composers) => {
      next((vm) => {
        vm.composers = new GroupedList(composers, {
          index: { field: 'name_sort', type: String }
        })
      })
    })
  },
  data() {
    return {
      composers: new GroupedList()
    }
  },
  computed: {
    heading() {
      return {
        subtitle: [{ count: this.composers.total, key: 'data.composers' }],
        title: this.$t('page.composers.title')
      }
    }
  }
}
</script>
