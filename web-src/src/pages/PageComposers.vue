<template>
  <div>
    <tabs-music />
    <content-with-heading>
      <template #options>
        <index-button-list :indices="composers.indices" />
      </template>
      <template #heading-left>
        <heading-title :content="heading" />
      </template>
      <template #content>
        <list-composers :items="composers" />
      </template>
    </content-with-heading>
  </div>
</template>

<script>
import ContentWithHeading from '@/templates/ContentWithHeading.vue'
import { GroupedList } from '@/lib/GroupedList'
import HeadingTitle from '@/components/HeadingTitle.vue'
import IndexButtonList from '@/components/IndexButtonList.vue'
import ListComposers from '@/components/ListComposers.vue'
import TabsMusic from '@/components/TabsMusic.vue'
import webapi from '@/webapi'

const dataObject = {
  load(to) {
    return webapi.library_composers('music')
  },
  set(vm, response) {
    vm.composers = new GroupedList(response.data, {
      index: { field: 'name_sort', type: String }
    })
  }
}

export default {
  name: 'PageComposers',
  components: {
    ContentWithHeading,
    HeadingTitle,
    IndexButtonList,
    ListComposers,
    TabsMusic
  },
  beforeRouteEnter(to, from, next) {
    dataObject.load(to).then((response) => {
      next((vm) => dataObject.set(vm, response))
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
        title: this.$t('page.composers.title'),
        subtitle: [{ key: 'count.composers', count: this.composers.total }]
      }
    }
  }
}
</script>
