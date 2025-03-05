<template>
  <div>
    <content-with-heading>
      <template #options>
        <index-button-list :indices="albums.indices" />
      </template>
      <template #heading-left>
        <heading-title :content="heading" />
      </template>
      <template #heading-right>
        <control-button
          :button="{ handler: showDetails, icon: 'dots-horizontal' }"
        />
        <control-button
          :button="{ handler: play, icon: 'shuffle', key: 'actions.shuffle' }"
        />
      </template>
      <template #content>
        <list-albums :items="albums" />
        <modal-dialog-genre
          :item="genre"
          :media_kind="media_kind"
          :show="show_details_modal"
          @close="show_details_modal = false"
        />
      </template>
    </content-with-heading>
  </div>
</template>

<script>
import ContentWithHeading from '@/templates/ContentWithHeading.vue'
import ControlButton from '@/components/ControlButton.vue'
import { GroupedList } from '@/lib/GroupedList'
import HeadingTitle from '@/components/HeadingTitle.vue'
import IndexButtonList from '@/components/IndexButtonList.vue'
import ListAlbums from '@/components/ListAlbums.vue'
import ModalDialogGenre from '@/components/ModalDialogGenre.vue'
import webapi from '@/webapi'

const dataObject = {
  load(to) {
    return Promise.all([
      webapi.library_genre(to.params.name, to.query.media_kind),
      webapi.library_genre_albums(to.params.name, to.query.media_kind)
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
    IndexButtonList,
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
      media_kind: this.$route.query.media_kind,
      show_details_modal: false
    }
  },
  computed: {
    heading() {
      return {
        title: this.genre.name,
        subtitle: [
          { key: 'count.albums', count: this.genre.album_count },
          {
            handler: this.open_tracks,
            key: 'count.tracks',
            count: this.genre.track_count
          }
        ]
      }
    }
  },
  methods: {
    open_tracks() {
      this.show_details_modal = false
      this.$router.push({
        name: 'genre-tracks',
        params: { name: this.genre.name },
        query: { media_kind: this.media_kind }
      })
    },
    play() {
      webapi.player_play_expression(
        `genre is "${this.genre.name}" and media_kind is ${this.media_kind}`,
        true
      )
    },
    showDetails() {
      this.show_details_modal = true
    }
  }
}
</script>
