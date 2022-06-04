<template>
  <div>
    <content-with-heading>
      <template #heading-left>
        <p class="title is-4" v-text="composer.name" />
      </template>
      <template #heading-right>
        <div class="buttons is-centered">
          <a
            class="button is-small is-light is-rounded"
            @click="show_composer_details_modal = true"
          >
            <mdicon class="icon" name="dots-horizontal" size="16" />
          </a>
          <a class="button is-small is-dark is-rounded" @click="play">
            <mdicon class="icon" name="shuffle" size="16" />
            <span v-text="$t('page.composer.shuffle')" />
          </a>
        </div>
      </template>
      <template #content>
        <p class="heading has-text-centered-mobile">
          <a
            class="has-text-link"
            @click="open_albums"
            v-text="
              $t('page.composer.album-count', {
                count: composer.album_count
              })
            "
          />
          <span>&nbsp;|&nbsp;</span>
          <span
            v-text="
              $t('page.composer.track-count', { count: composer.track_count })
            "
          />
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
