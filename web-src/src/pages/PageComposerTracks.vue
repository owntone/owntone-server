<template>
  <div>
    <content-with-heading>
      <template #heading-left>
        <p class="title is-4">
          {{ composer.name }}
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
          <a class="has-text-link" @click="open_albums"
            >{{ composer.album_count }} albums</a
          >
          | {{ composer.track_count }} tracks
        </p>
        <list-tracks :tracks="tracks.items" :expression="play_expression" />
        <modal-dialog-composer
          :show="show_composer_details_modal"
          :composer="composer"
          @close="show_composer_details_modal = false"
        />
      </template>
    </content-with-heading>
  </div>
</template>

<script>
import ContentWithHeading from '@/templates/ContentWithHeading.vue'
import ListTracks from '@/components/ListTracks.vue'
import ModalDialogComposer from '@/components/ModalDialogComposer.vue'
import webapi from '@/webapi'

const dataObject = {
  load: function (to) {
    return Promise.all([
      webapi.library_composer(to.params.composer),
      webapi.library_composer_tracks(to.params.composer)
    ])
  },

  set: function (vm, response) {
    vm.composer = response[0].data
    vm.tracks = response[1].data.tracks
  }
}

export default {
  name: 'PageComposerTracks',
  components: {
    ContentWithHeading,
    ListTracks,
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
      composer: {},

      show_composer_details_modal: false
    }
  },

  computed: {
    play_expression() {
      return 'composer is "' + this.composer.name + '" and media_kind is music'
    }
  },

  methods: {
    open_albums: function () {
      this.show_details_modal = false
      this.$router.push({
        name: 'ComposerAlbums',
        params: { composer: this.composer.name }
      })
    },

    play: function () {
      webapi.player_play_expression(this.play_expression, true)
    }
  }
}
</script>

<style></style>
