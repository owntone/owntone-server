<template>
  <tabs-music />
  <content-with-heading>
    <template #options>
      <list-index-buttons :indices="composers.indices" />
    </template>
    <template #heading>
      <pane-title :content="heading" />
    </template>
    <template #content>
      <list-composers :items="composers" />
    </template>
  </content-with-heading>
</template>

<script>
import ContentWithHeading from '@/templates/ContentWithHeading.vue'
import { GroupedList } from '@/lib/GroupedList'
import PaneTitle from '@/components/PaneTitle.vue'
import ListComposers from '@/components/ListComposers.vue'
import ListIndexButtons from '@/components/ListIndexButtons.vue'
import TabsMusic from '@/components/TabsMusic.vue'
import library from '@/api/library'

export default {
  name: 'PageComposers',
  components: {
    ContentWithHeading,
    PaneTitle,
    ListIndexButtons,
    ListComposers,
    TabsMusic
  },
  beforeRouteEnter(to, from, next) {
    library.composers('music').then((composers) => {
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
