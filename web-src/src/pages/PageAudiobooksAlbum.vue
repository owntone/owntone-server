<template>
  <div>
    <content-with-hero>
      <template #heading>
        <heading-hero :content="heading" />
      </template>
      <template #image>
        <control-image
          :url="album.artwork_url"
          :caption="album.name"
          class="is-clickable is-medium"
          @click="openDetails"
        />
      </template>
      <template #content>
        <list-tracks :items="tracks" :show-progress="true" :uris="album.uri" />
        <modal-dialog-album
          :item="album"
          :show="showDetailsModal"
          :media_kind="'audiobook'"
          @close="showDetailsModal = false"
        />
      </template>
    </content-with-hero>
  </div>
</template>

<script>
import ContentWithHero from '@/templates/ContentWithHero.vue'
import ControlImage from '@/components/ControlImage.vue'
import { GroupedList } from '@/lib/GroupedList'
import HeadingHero from '@/components/HeadingHero.vue'
import ListTracks from '@/components/ListTracks.vue'
import ModalDialogAlbum from '@/components/ModalDialogAlbum.vue'
import webapi from '@/webapi'

const dataObject = {
  load(to) {
    return Promise.all([
      webapi.library_album(to.params.id),
      webapi.library_album_tracks(to.params.id)
    ])
  },
  set(vm, response) {
    vm.album = response[0].data
    vm.tracks = new GroupedList(response[1].data)
  }
}

export default {
  name: 'PageAudiobooksAlbum',
  components: {
    ContentWithHero,
    ControlImage,
    HeadingHero,
    ListTracks,
    ModalDialogAlbum
  },
  beforeRouteEnter(to, from, next) {
    dataObject.load(to).then((response) => {
      next((vm) => dataObject.set(vm, response))
    })
  },
  data() {
    return {
      album: {},
      showDetailsModal: false,
      tracks: new GroupedList()
    }
  },
  computed: {
    heading() {
      return {
        count: this.$t('count.tracks', { count: this.album.track_count }),
        handler: this.openArtist,
        subtitle: this.album.artist,
        title: this.album.name,
        actions: [
          { handler: this.play, icon: 'play', key: 'actions.play' },
          { handler: this.openDetails, icon: 'dots-horizontal' }
        ]
      }
    }
  },
  methods: {
    openArtist() {
      this.showDetailsModal = false
      this.$router.push({
        name: 'audiobooks-artist',
        params: { id: this.album.artist_id }
      })
    },
    openDetails() {
      this.showDetailsModal = true
    },
    play() {
      webapi.player_play_uri(this.album.uri, false)
    }
  }
}
</script>
