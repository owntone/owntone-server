<template>
  <div class="fd-page">
    <content-with-heading>
      <template #heading-left>
        <p class="title is-4" v-text="$t('page.radio.title')" />
        <p
          class="heading has-text-centered-mobile"
          v-text="$t('page.radio.count', { count: tracks.total })"
        />
      </template>
      <template #content>
        <list-tracks :tracks="tracks" />
      </template>
    </content-with-heading>
  </div>
</template>

<script>
import ContentWithHeading from '@/templates/ContentWithHeading.vue'
import { GroupByList } from '@/lib/GroupByList'
import ListTracks from '@/components/ListTracks.vue'
import webapi from '@/webapi'

const dataObject = {
  load(to) {
    return webapi.library_radio_streams()
  },

  set(vm, response) {
    vm.tracks = new GroupByList(response.data.tracks)
  }
}

export default {
  name: 'PageRadioStreams',
  components: { ContentWithHeading, ListTracks },

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
      tracks: new GroupByList()
    }
  }
}
</script>

<style></style>
