<template>
  <div>
    <content-with-heading>
      <template #options>
        <index-button-list :index="index_list" />
      </template>
      <template #heading-left>
        <p class="title is-4" v-text="artist.name" />
      </template>
      <template #heading-right>
        <div class="buttons is-centered">
          <a
            class="button is-small is-light is-rounded"
            @click="show_artist_details_modal = true"
          >
            <span class="icon"
              ><mdicon name="dots-horizontal" size="16"
            /></span>
          </a>
          <a class="button is-small is-dark is-rounded" @click="play">
            <span class="icon"><mdicon name="shuffle" size="16" /></span>
            <span v-text="$t('page.artist.shuffle')" />
          </a>
        </div>
      </template>
      <template #content>
        <p class="heading has-text-centered-mobile">
          <a
            class="has-text-link"
            @click="open_artist"
            v-text="
              $t('page.artist.album-count', { count: artist.album_count })
            "
          />
          <span>&nbsp;|&nbsp;</span>
          <span
            v-text="
              $t('page.artist.track-count', { count: artist.track_count })
            "
          />
        </p>
        <list-tracks :tracks="tracks.items" :uris="track_uris" />
        <modal-dialog-artist
          :show="show_artist_details_modal"
          :artist="artist"
          @close="show_artist_details_modal = false"
        />
      </template>
    </content-with-heading>
  </div>
</template>

<script>
import ContentWithHeading from '@/templates/ContentWithHeading.vue'
import IndexButtonList from '@/components/IndexButtonList.vue'
import ListTracks from '@/components/ListTracks.vue'
import ModalDialogArtist from '@/components/ModalDialogArtist.vue'
import webapi from '@/webapi'

const dataObject = {
  load: function (to) {
    return Promise.all([
      webapi.library_artist(to.params.artist_id),
      webapi.library_artist_tracks(to.params.artist_id)
    ])
  },

  set: function (vm, response) {
    vm.artist = response[0].data
    vm.tracks = response[1].data.tracks
  }
}

export default {
  name: 'PageArtistTracks',
  components: {
    ContentWithHeading,
    ListTracks,
    IndexButtonList,
    ModalDialogArtist
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
      artist: {},
      tracks: { items: [] },

      show_artist_details_modal: false
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

    track_uris() {
      return this.tracks.items.map((a) => a.uri).join(',')
    }
  },

  methods: {
    open_artist: function () {
      this.show_details_modal = false
      this.$router.push({ path: '/music/artists/' + this.artist.id })
    },

    play: function () {
      webapi.player_play_uri(
        this.tracks.items.map((a) => a.uri).join(','),
        true
      )
    }
  }
}
</script>

<style></style>
