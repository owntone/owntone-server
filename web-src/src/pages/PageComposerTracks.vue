<template>
  <div>
    <content-with-heading>
      <template #options>
        <index-button-list :index="index_list" />
      </template>
      <template #heading-left>
        <p class="title is-4">
          {{ composer }}
        </p>
      </template>
      <template #heading-right>
        <div class="buttons is-centered">
          <a
            class="button is-small is-light is-rounded"
            @click="show_composer_details_modal = true"
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
          <a class="has-text-link" @click="open_albums">albums</a> |
          {{ tracks.total }} tracks
        </p>
        <list-item-track
          v-for="(track, index) in rated_tracks"
          :key="track.id"
          :track="track"
          @click="play_track(index)"
        >
          <template #actions>
            <a @click.prevent.stop="open_dialog(track)">
              <span class="icon has-text-dark"
                ><i class="mdi mdi-dots-vertical mdi-18px"
              /></span>
            </a>
          </template>
        </list-item-track>
        <modal-dialog-track
          :show="show_details_modal"
          :track="selected_track"
          @close="show_details_modal = false"
        />
        <modal-dialog-composer
          :show="show_composer_details_modal"
          :composer="{ name: composer }"
          @close="show_composer_details_modal = false"
        />
      </template>
    </content-with-heading>
  </div>
</template>

<script>
import ContentWithHeading from '@/templates/ContentWithHeading.vue'
import ListItemTrack from '@/components/ListItemTrack.vue'
import ModalDialogTrack from '@/components/ModalDialogTrack.vue'
import ModalDialogComposer from '@/components/ModalDialogComposer.vue'
import webapi from '@/webapi'

const dataObject = {
  load: function (to) {
    return webapi.library_composer_tracks(to.params.composer)
  },

  set: function (vm, response) {
    vm.composer = vm.$route.params.composer
    vm.tracks = response.data.tracks
  }
}

export default {
  name: 'PageComposerTracks',
  components: {
    ContentWithHeading,
    ListItemTrack,
    ModalDialogTrack,
    ModalDialogComposer
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
      composer: '',

      min_rating: 0,

      show_details_modal: false,
      selected_track: {},

      show_composer_details_modal: false
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

    rated_tracks() {
      return this.tracks.items.filter(
        (track) => track.rating >= this.min_rating
      )
    }
  },

  methods: {
    open_albums: function () {
      this.show_details_modal = false
      this.$router.push({
        name: 'ComposerAlbums',
        params: { composer: this.composer }
      })
    },

    play: function () {
      webapi.player_play_expression(
        'composer is "' + this.composer + '" and media_kind is music',
        true
      )
    },

    play_track: function (position) {
      webapi.player_play_expression(
        'composer is "' + this.composer + '" and media_kind is music',
        false,
        position
      )
    },

    show_rating: function (rating) {
      if (rating === 0.5) {
        rating = 0
      }
      this.min_rating = Math.ceil(rating) * 20
    },

    open_dialog: function (track) {
      this.selected_track = track
      this.show_details_modal = true
    }
  }
}
</script>

<style></style>
