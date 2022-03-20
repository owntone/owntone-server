<template>
  <div class="fd-page-with-tabs">
    <tabs-music />

    <content-with-heading>
      <template #heading-left>
        <p class="title is-4">Recently played</p>
        <p class="heading">tracks</p>
      </template>
      <template #content>
        <list-tracks :tracks="recently_played.items" />
      </template>
    </content-with-heading>
  </div>
</template>

<script>
import ContentWithHeading from '@/templates/ContentWithHeading.vue'
import TabsMusic from '@/components/TabsMusic.vue'
import ListTracks from '@/components/ListTracks.vue'
import webapi from '@/webapi'

const dataObject = {
  load: function (to) {
    return webapi.search({
      type: 'track',
      expression:
        'time_played after 8 weeks ago and media_kind is music order by time_played desc',
      limit: 50
    })
  },

  set: function (vm, response) {
    vm.recently_played = response.data.tracks
  }
}

export default {
  name: 'PageBrowseType',
  components: { ContentWithHeading, TabsMusic, ListTracks },

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
      recently_played: {}
    }
  }
}
</script>

<style></style>
