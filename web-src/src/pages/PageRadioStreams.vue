<template>
  <content-with-heading>
    <template #heading>
      <heading-title :content="heading" />
    </template>
    <template #content>
      <list-tracks :items="tracks" />
    </template>
  </content-with-heading>
</template>

<script>
import ContentWithHeading from '@/templates/ContentWithHeading.vue'
import { GroupedList } from '@/lib/GroupedList'
import HeadingTitle from '@/components/HeadingTitle.vue'
import ListTracks from '@/components/ListTracks.vue'
import webapi from '@/webapi'

export default {
  name: 'PageRadioStreams',
  components: { ContentWithHeading, HeadingTitle, ListTracks },
  beforeRouteEnter(to, from, next) {
    webapi.library_radio_streams().then((tracks) => {
      next((vm) => {
        vm.tracks = new GroupedList(tracks)
      })
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
        subtitle: [{ count: this.tracks.total, key: 'data.stations' }],
        title: this.$t('page.radio.title')
      }
    }
  }
}
</script>
