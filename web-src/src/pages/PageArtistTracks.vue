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
              v-model:value="selected_grouping_option_id"
              :options="grouping_options"
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
        <list-tracks :tracks="tracks" :uris="track_uris" />
        <modal-dialog-artist
          :show="show_details_modal"
          :artist="artist"
          @close="show_details_modal = false"
        />
      </template>
    </content-with-heading>
  </div>
</template>

<script>
import * as types from '@/store/mutation_types'
import { GroupedList } from '@/lib/GroupedList'
import ContentWithHeading from '@/templates/ContentWithHeading.vue'
import ControlDropdown from '@/components/ControlDropdown.vue'
import IndexButtonList from '@/components/IndexButtonList.vue'
import ListTracks from '@/components/ListTracks.vue'
import ModalDialogArtist from '@/components/ModalDialogArtist.vue'
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
      grouping_options: [
        {
          id: 1,
          name: this.$t('page.artist.sort.name'),
          options: { index: { field: 'title_sort', type: String } }
        },
        {
          id: 2,
          name: this.$t('page.artist.sort.rating'),
          options: {
            criteria: [{ field: 'rating', type: Number, order: -1 }],
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
        return this.$store.state.hide_spotify
      },
      set(value) {
        this.$store.commit(types.HIDE_SPOTIFY, value)
      }
    },
    selected_grouping_option_id: {
      get() {
        return this.$store.state.artist_tracks_sort
      },
      set(value) {
        this.$store.commit(types.ARTIST_TRACKS_SORT, value)
      }
    },
    spotify_enabled() {
      return this.$store.state.spotify.webapi_token_valid
    },
    tracks() {
      const grouping = this.grouping_options.find(
        (o) => o.id === this.selected_grouping_option_id
      )
      grouping.options.filters = [
        (track) => !this.hide_spotify || track.data_kind !== 'spotify'
      ]
      this.tracks_list.group(grouping.options)
      return this.tracks_list
    },
    track_uris() {
      return this.tracks_list.items.map((a) => a.uri).join(',')
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
        this.tracks_list.items.map((a) => a.uri).join(','),
        true
      )
    }
  }
}
</script>

<style></style>
