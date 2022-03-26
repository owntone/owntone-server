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
          {{ composer.album_count }} albums |
          <a class="has-text-link" @click="open_tracks"
            >{{ composer.track_count }} tracks</a
          >
        </p>
        <list-albums :albums="albums_list" :hide_group_title="true" />

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
import ListAlbums from '@/components/ListAlbums.vue'
import ModalDialogComposer from '@/components/ModalDialogComposer.vue'
import webapi from '@/webapi'
import { GroupByList } from '@/lib/GroupByList'

const dataObject = {
  load: function (to) {
    return Promise.all([
      webapi.library_composer(to.params.composer),
      webapi.library_composer_albums(to.params.composer)
    ])
  },

  set: function (vm, response) {
    vm.composer = response[0].data
    vm.albums_list = new GroupByList(response[1].data.albums)
  }
}

export default {
  name: 'PageComposer',
  components: {
    ContentWithHeading,
    ListAlbums,
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
      composer: {},
      albums_list: new GroupByList(),
      show_composer_details_modal: false
    }
  },

  methods: {
    open_tracks: function () {
      this.$router.push({
        name: 'ComposerTracks',
        params: { composer: this.composer.name }
      })
    },

    play: function () {
      webapi.player_play_expression(
        'composer is "' + this.composer.name + '" and media_kind is music',
        true
      )
    }
  }
}
</script>

<style></style>
