<template>
  <div>
    <content-with-heading>
      <template #heading-left>
        <heading-title :content="heading" />
      </template>
      <template #content>
        <list-tracks :items="tracks" />
      </template>
    </content-with-heading>
  </div>
</template>

<script>
import ContentWithHeading from '@/templates/ContentWithHeading.vue'
import { GroupedList } from '@/lib/GroupedList'
import HeadingTitle from '@/components/HeadingTitle.vue'
import ListTracks from '@/components/ListTracks.vue'
import webapi from '@/webapi'

const dataObject = {
  load(to) {
    return webapi.library_radio_streams()
  },
  set(vm, response) {
    vm.tracks = new GroupedList(response.data.tracks)
  }
}

export default {
  name: 'PageRadioStreams',
  components: { ContentWithHeading, HeadingTitle, ListTracks },
  beforeRouteEnter(to, from, next) {
    dataObject.load(to).then((response) => {
      next((vm) => dataObject.set(vm, response))
    })
  },
  data() {
    return {
      tracks: new GroupedList()
    }
  },
  computed: {
    heading() {
      return {
        subtitle: [{ count: this.tracks.total, key: 'count.stations' }],
        title: this.$t('page.radio.title')
      }
    }
  }
}
</script>
