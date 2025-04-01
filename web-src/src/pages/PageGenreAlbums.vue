<template>
  <content-with-heading>
    <template #options>
      <list-index-buttons :indices="albums.indices" />
    </template>
    <template #heading>
      <heading-title :content="heading" />
    </template>
    <template #actions>
      <control-button
        :button="{ handler: openDetails, icon: 'dots-horizontal' }"
      />
      <control-button
        :button="{ handler: play, icon: 'shuffle', key: 'actions.shuffle' }"
      />
    </template>
    <template #content>
      <list-albums :items="albums" />
    </template>
  </content-with-heading>
  <modal-dialog-genre
    :item="genre"
    :media-kind="mediaKind"
    :show="showDetailsModal"
    @close="showDetailsModal = false"
  />
</template>

<script>
import ContentWithHeading from '@/templates/ContentWithHeading.vue'
import ControlButton from '@/components/ControlButton.vue'
import { GroupedList } from '@/lib/GroupedList'
import HeadingTitle from '@/components/HeadingTitle.vue'
import ListAlbums from '@/components/ListAlbums.vue'
import ListIndexButtons from '@/components/ListIndexButtons.vue'
import ModalDialogGenre from '@/components/ModalDialogGenre.vue'
import webapi from '@/webapi'

const dataObject = {
  load(to) {
    return Promise.all([
      webapi.library_genre(to.params.name, to.query.mediaKind),
      webapi.library_genre_albums(to.params.name, to.query.mediaKind)
    ])
  },
  set(vm, response) {
    vm.genre = response[0].data.genres.items.shift()
    vm.albums = new GroupedList(response[1].data.albums, {
      index: { field: 'name_sort', type: String }
    })
  }
}

export default {
  name: 'PageGenreAlbums',
  components: {
    ContentWithHeading,
    ControlButton,
    HeadingTitle,
    ListIndexButtons,
    ListAlbums,
    ModalDialogGenre
  },
  beforeRouteEnter(to, from, next) {
    dataObject.load(to).then((response) => {
      next((vm) => dataObject.set(vm, response))
    })
  },
  data() {
    return {
      albums: new GroupedList(),
      genre: {},
      mediaKind: this.$route.query.mediaKind,
      showDetailsModal: false
    }
  },
  computed: {
    heading() {
      if (this.genre.name) {
        return {
          subtitle: [
            { count: this.genre.album_count, key: 'count.albums' },
            {
              count: this.genre.track_count,
              handler: this.openTracks,
              key: 'count.tracks'
            }
          ],
          title: this.genre.name
        }
      }
      return {}
    }
  },
  methods: {
    openDetails() {
      this.showDetailsModal = true
    },
    openTracks() {
      this.showDetailsModal = false
      this.$router.push({
        name: 'genre-tracks',
        params: { name: this.genre.name },
        query: { mediaKind: this.mediaKind }
      })
    },
    play() {
      webapi.player_play_expression(
        `genre is "${this.genre.name}" and media_kind is ${this.mediaKind}`,
        true
      )
    }
  }
}
</script>
