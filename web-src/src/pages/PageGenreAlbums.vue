<template>
  <div>
    <content-with-heading>
      <template #options>
        <index-button-list :indices="albums.indices" />
      </template>
      <template #heading-left>
        <p class="title is-4" v-text="genre.name" />
      </template>
      <template #heading-right>
        <div class="buttons is-centered">
          <a
            class="button is-small is-light is-rounded"
            @click="show_details_modal = true"
          >
            <mdicon class="icon" name="dots-horizontal" size="16" />
          </a>
          <a class="button is-small is-dark is-rounded" @click="play">
            <mdicon class="icon" name="shuffle" size="16" />
            <span v-text="$t('page.genre.shuffle')" />
          </a>
        </div>
      </template>
      <template #content>
        <p class="heading has-text-centered-mobile">
          <span
            v-text="$t('page.genre.album-count', { count: genre.album_count })"
          />
          <span>&nbsp;|&nbsp;</span>
          <a
            class="has-text-link"
            @click="open_tracks"
            v-text="$t('page.genre.track-count', { count: genre.track_count })"
          />
        </p>
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
import { GroupedList } from '@/lib/GroupedList'
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
    vm.genre = response[0].data.genres.items[0]
    vm.albums = new GroupedList(response[1].data.albums, {
      index: { field: 'name_sort', type: String }
    })
  }
}

export default {
  name: 'PageGenreAlbums',
  components: {
    ContentWithHeading,
    IndexButtonList,
    ListAlbums,
    ModalDialogGenre
  },
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
      albums: new GroupedList(),
      genre: {},
      media_kind: this.$route.query.media_kind,
      show_details_modal: false
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
    }
  }
}
</script>

<style></style>
