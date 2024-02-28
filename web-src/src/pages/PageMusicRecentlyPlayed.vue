<template>
  <div class="fd-page-with-tabs">
    <tabs-music />
    <content-with-heading>
      <template #heading-left>
        <p class="title is-4" v-text="$t('page.music.recently-played.title')" />
      </template>
      <template #content>
        <list-tracks :tracks="recently_played" />
      </template>
    </content-with-heading>
  </div>
</template>

<script>
import ContentWithHeading from '@/templates/ContentWithHeading.vue'
import { GroupedList } from '@/lib/GroupedList'
import ListTracks from '@/components/ListTracks.vue'
import TabsMusic from '@/components/TabsMusic.vue'
import webapi from '@/webapi'

const dataObject = {
  load(to) {
    return webapi.search({
      type: 'track',
      expression:
        'time_played after 8 weeks ago and media_kind is music order by time_played desc',
      limit: 50
    })
  },

  set(vm, response) {
    vm.recently_played = new GroupedList(response.data.tracks)
  }
}

export default {
  name: 'PageMusicRecentlyPlayed',
  components: { ContentWithHeading, ListTracks, TabsMusic },

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
