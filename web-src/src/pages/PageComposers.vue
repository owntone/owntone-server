<template>
  <div class="fd-page-with-tabs">
    <tabs-music />
    <content-with-heading>
      <template #options>
        <index-button-list :indices="composers.indices" />
      </template>
      <template #heading-left>
        <p class="title is-4" v-text="$t('page.composers.title')" />
        <p
          class="heading"
          v-text="$t('page.composers.count', { count: composers.total })"
        />
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
  components: { ContentWithHeading, IndexButtonList, ListComposers, TabsMusic },

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
      composers: new GroupedList()
    }
  }
}
</script>

<style></style>
