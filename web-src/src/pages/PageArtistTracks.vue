<template>
  <div>
    <content-with-heading>
      <template #options>
        <index-button-list :indices="tracks.indices" />
        <div class="columns">
          <div class="column">
            <p class="heading mb-5" v-text="$t('page.artist.filter')" />
            <div v-if="spotify_enabled" class="field">
              <div class="control">
                <input
                  id="switchHideSpotify"
                  v-model="hide_spotify"
                  type="checkbox"
                  class="switch is-rounded"
                />
                <label
                  for="switchHideSpotify"
                  v-text="$t('page.artist.hide-spotify')"
                />
              </div>
              <p class="help" v-text="$t('page.artist.hide-spotify-help')" />
            </div>
          </div>
          <div class="column">
            <p class="heading mb-5" v-text="$t('page.artist.sort.title')" />
            <control-dropdown
              v-model:value="selected_grouping_id"
              :options="groupings"
            />
          </div>
        </div>
      </template>
      <template #heading-left>
        <p class="title is-4" v-text="artist.name" />
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
            <span v-text="$t('page.artist.shuffle')" />
          </a>
        </div>
      </template>
      <template #content>
        <p class="heading has-text-centered-mobile">
          <a
            class="has-text-link"
            @click="open_artist"
            v-text="$t('page.artist.album-count', { count: album_count })"
          />
          <span>&nbsp;|&nbsp;</span>
          <span
            v-text="$t('page.artist.track-count', { count: tracks.count })"
          />
        </p>
        <list-tracks :items="tracks" :uris="track_uris" />
        <modal-dialog-artist
          :item="artist"
          :show="show_details_modal"
          @close="show_details_modal = false"
        />
      </template>
    </content-with-heading>
  </div>
</template>

<script>
import ContentWithHeading from '@/templates/ContentWithHeading.vue'
import ControlDropdown from '@/components/ControlDropdown.vue'
import { GroupedList } from '@/lib/GroupedList'
import IndexButtonList from '@/components/IndexButtonList.vue'
import ListTracks from '@/components/ListTracks.vue'
import ModalDialogArtist from '@/components/ModalDialogArtist.vue'
import { useServicesStore } from '@/stores/services'
import { useUIStore } from '@/stores/ui'
import webapi from '@/webapi'

const dataObject = {
  load(to) {
    return Promise.all([
      webapi.library_artist(to.params.id),
      webapi.library_artist_tracks(to.params.id)
    ])
  },

  set(vm, response) {
    vm.artist = response[0].data
    vm.tracks_list = new GroupedList(response[1].data.tracks)
  }
}

export default {
  name: 'PageArtistTracks',
  components: {
    ContentWithHeading,
    ControlDropdown,
    IndexButtonList,
    ListTracks,
    ModalDialogArtist
  },

  beforeRouteEnter(to, from, next) {
    dataObject.load(to).then((response) => {
      next((vm) => dataObject.set(vm, response))
    })
  },

  setup() {
    return { servicesStore: useServicesStore(), uiStore: useUIStore() }
  },

  data() {
    return {
      artist: {},
      groupings: [
        {
          id: 1,
          name: this.$t('page.artist.sort.name'),
          options: { index: { field: 'title_sort', type: String } }
        },
        {
          id: 2,
          name: this.$t('page.artist.sort.rating'),
          options: {
            criteria: [{ field: 'rating', order: -1, type: Number }],
            index: { field: 'rating', type: 'Digits' }
          }
        }
      ],
      show_details_modal: false,
      tracks_list: new GroupedList()
    }
  },

  computed: {
    album_count() {
      return new Set(
        [...this.tracks]
          .filter((track) => track.isItem)
          .map((track) => track.item.album_id)
      ).size
    },
    hide_spotify: {
      get() {
        return this.uiStore.hide_spotify
      },
      set(value) {
        this.uiStore.hide_spotify = value
      }
    },
    selected_grouping_id: {
      get() {
        return this.uiStore.artist_tracks_sort
      },
      set(value) {
        this.uiStore.artist_tracks_sort = value
      }
    },
    spotify_enabled() {
      return this.servicesStore.spotify.webapi_token_valid
    },
    track_uris() {
      return this.tracks_list.items.map((item) => item.uri).join()
    },
    tracks() {
      const { options } = this.groupings.find(
        (grouping) => grouping.id === this.selected_grouping_id
      )
      options.filters = [
        (track) => !this.hide_spotify || track.data_kind !== 'spotify'
      ]
      return this.tracks_list.group(options)
    }
  },

  methods: {
    open_artist() {
      this.show_details_modal = false
      this.$router.push({
        name: 'music-artist',
        params: { id: this.artist.id }
      })
    },
    play() {
      webapi.player_play_uri(
        this.tracks_list.items.map((item) => item.uri).join(),
        true
      )
    }
  }
}
</script>

<style></style>
