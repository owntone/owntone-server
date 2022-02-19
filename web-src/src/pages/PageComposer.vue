<template>
  <div>
    <content-with-heading>
      <template #options>
        <index-button-list :index="index_list" />
      </template>
      <template #heading-left>
        <p class="title is-4">
          {{ name }}
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
          {{ composer_albums.total }} albums |
          <a class="has-text-link" @click="open_tracks">tracks</a>
        </p>
        <list-item-albums
          v-for="album in composer_albums.items"
          :key="album.id"
          :album="album"
          @click="open_album(album)"
        >
          <template slot:actions>
            <a @click="open_dialog(album)">
              <span class="icon has-text-dark"
                ><i class="mdi mdi-dots-vertical mdi-18px"
              /></span>
            </a>
          </template>
        </list-item-albums>
        <modal-dialog-album
          :show="show_details_modal"
          :album="selected_album"
          @close="show_details_modal = false"
        />
        <modal-dialog-composer
          :show="show_composer_details_modal"
          :composer="{ name: name }"
          @close="show_composer_details_modal = false"
        />
      </template>
    </content-with-heading>
  </div>
</template>

<script>
import ContentWithHeading from '@/templates/ContentWithHeading.vue'
import ListItemAlbums from '@/components/ListItemAlbum.vue'
import ModalDialogAlbum from '@/components/ModalDialogAlbum.vue'
import ModalDialogComposer from '@/components/ModalDialogComposer.vue'
import webapi from '@/webapi'

const dataObject = {
  load: function (to) {
    return webapi.library_composer(to.params.composer)
  },

  set: function (vm, response) {
    vm.name = vm.$route.params.composer
    vm.composer_albums = response.data.albums
  }
}

export default {
  name: 'PageComposer',
  components: {
    ContentWithHeading,
    ListItemAlbums,
    ModalDialogAlbum,
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
      name: '',
      composer_albums: { items: [] },
      show_details_modal: false,
      selected_album: {},

      show_composer_details_modal: false
    }
  },

  computed: {
    index_list() {
      return [
        ...new Set(
          this.composer_albums.items.map((album) =>
            album.name_sort.charAt(0).toUpperCase()
          )
        )
      ]
    }
  },

  methods: {
    open_tracks: function () {
      this.show_details_modal = false
      this.$router.push({
        name: 'ComposerTracks',
        params: { composer: this.name }
      })
    },

    play: function () {
      webapi.player_play_expression(
        'composer is "' + this.name + '" and media_kind is music',
        true
      )
    },

    open_album: function (album) {
      this.$router.push({ path: '/music/albums/' + album.id })
    },

    open_dialog: function (album) {
      this.selected_album = album
      this.show_details_modal = true
    }
  }
}
</script>

<style></style>
