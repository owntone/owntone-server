<template>
  <div class="fd-page-with-tabs">
    <tabs-music />
    <content-with-heading>
      <template #options>
        <index-button-list :index="composers.indexList" />
      </template>
      <template #heading-left>
        <p class="title is-4" v-text="$t('page.composers.title')" />
        <p
          class="heading"
          v-text="$t('page.composers.count', { count: composers.total })"
        />
      </template>
      <template #content>
        <list-composers :composers="composers" />
      </template>
    </content-with-heading>
  </div>
</template>

<script>
import ContentWithHeading from '@/templates/ContentWithHeading.vue'
import { GroupByList, byName } from '@/lib/GroupByList'
import IndexButtonList from '@/components/IndexButtonList.vue'
import ListComposers from '@/components/ListComposers.vue'
import TabsMusic from '@/components/TabsMusic.vue'
import webapi from '@/webapi'

const dataObject = {
  load(to) {
    return webapi.library_composers('music')
  },

  set(vm, response) {
    vm.composers = new GroupByList(response.data)
    vm.composers.group(byName('name_sort'))
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
    if (!this.composers.isEmpty()) {
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
      composers: new GroupByList()
    }
  }
}
</script>

<style></style>
