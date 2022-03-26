<template>
  <div>
    <tabs-music />

    <content-with-heading>
      <template #options>
        <index-button-list :index="composers.indexList" />
      </template>
      <template #heading-left>
        <p class="title is-4">Composers</p>
        <p class="heading">{{ composers.total }} composers</p>
      </template>
      <template #content>
        <list-composers :composers="composers" />
      </template>
    </content-with-heading>
  </div>
</template>

<script>
import ContentWithHeading from '@/templates/ContentWithHeading.vue'
import TabsMusic from '@/components/TabsMusic.vue'
import IndexButtonList from '@/components/IndexButtonList.vue'
import ListComposers from '@/components/ListComposers.vue'
import webapi from '@/webapi'
import { byName, GroupByList } from '@/lib/GroupByList'

const dataObject = {
  load: function (to) {
    return webapi.library_composers('music')
  },

  set: function (vm, response) {
    vm.composers = new GroupByList(response.data)
    vm.composers.group(byName('name_sort'))
  }
}

export default {
  name: 'PageComposers',
  components: { ContentWithHeading, TabsMusic, IndexButtonList, ListComposers },

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
  },

  methods: {}
}
</script>

<style></style>
