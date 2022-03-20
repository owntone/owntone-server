<template>
  <div>
    <content-with-heading>
      <template #options>
        <index-button-list :index="index_list" />
      </template>
      <template #heading-left>
        <p class="title is-4">
          {{ genre }}
        </p>
      </template>
      <template #heading-right>
        <div class="buttons is-centered">
          <a
            class="button is-small is-light is-rounded"
            @click="show_genre_details_modal = true"
          >
            <span class="icon"
              ><i class="mdi mdi-dots-horizontal mdi-18px"
            /></span>
          </a>
          <a class="button is-small is-dark is-rounded" @click="play">
            <span class="icon"><i class="mdi mdi-shuffle" /></span>
            <span>Shuffle</span>
          </a>
        </div>
      </template>
      <template #content>
        <p class="heading has-text-centered-mobile">
          <a class="has-text-link" @click="open_genre">albums</a> |
          {{ tracks.total }} tracks
        </p>
        <list-tracks :tracks="tracks.items" :expression="expression" />
        <modal-dialog-genre
          :show="show_genre_details_modal"
          :genre="{ name: genre }"
          @close="show_genre_details_modal = false"
        />
      </template>
    </content-with-heading>
  </div>
</template>

<script>
import ContentWithHeading from '@/templates/ContentWithHeading.vue'
import IndexButtonList from '@/components/IndexButtonList.vue'
import ListTracks from '@/components/ListTracks.vue'
import ModalDialogGenre from '@/components/ModalDialogGenre.vue'
import webapi from '@/webapi'

const dataObject = {
  load: function (to) {
    return webapi.library_genre_tracks(to.params.genre)
  },

  set: function (vm, response) {
    vm.genre = vm.$route.params.genre
    vm.tracks = response.data.tracks
  }
}

export default {
  name: 'PageGenreTracks',
  components: {
    ContentWithHeading,
    ListTracks,
    IndexButtonList,
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
      tracks: { items: [] },
      genre: '',

      show_genre_details_modal: false
    }
  },

  computed: {
    index_list() {
      return [
        ...new Set(
          this.tracks.items.map((track) =>
            track.title_sort.charAt(0).toUpperCase()
          )
        )
      ]
    },

    expression() {
      return 'genre is "' + this.genre + '" and media_kind is music'
    }
  },

  methods: {
    open_genre: function () {
      this.show_details_modal = false
      this.$router.push({ name: 'Genre', params: { genre: this.genre } })
    },

    play: function () {
      webapi.player_play_expression(this.expression, true)
    }
  }
}
</script>

<style></style>
