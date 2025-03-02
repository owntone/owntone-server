<template>
  <div>
    <content-with-heading>
      <template #options>
        <index-button-list :indices="tracks.indices" />
        <div class="columns">
          <div class="column">
            <p
              class="is-size-7 is-uppercase"
              v-text="$t('page.artist.filter')"
            />
            <control-switch
              v-if="spotify_enabled"
              v-model="uiStore.hide_spotify"
            >
              <template #label>
                <span v-text="$t('page.artist.hide-spotify')" />
              </template>
              <template #help>
                <span v-text="$t('page.artist.hide-spotify-help')" />
              </template>
            </control-switch>
          </div>
          <div class="column">
            <p
              class="is-size-7 is-uppercase"
              v-text="$t('page.artist.sort.title')"
            />
            <control-dropdown
              v-model:value="uiStore.artist_tracks_sort"
              :options="groupings"
            />
          </div>
        </div>
      </template>
      <template #heading-left>
        <heading-title :content="heading" />
      </template>
      <template #heading-right>
        <control-button
          :button="{ handler: showDetails, icon: 'dots-horizontal' }"
        />
        <control-button
          :button="{
            handler: play,
            icon: 'shuffle',
            key: 'page.artist.shuffle'
          }"
        />
      </template>
      <template #content>
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
import ControlButton from '@/components/ControlButton.vue'
import ControlDropdown from '@/components/ControlDropdown.vue'
import ControlSwitch from '@/components/ControlSwitch.vue'
import { GroupedList } from '@/lib/GroupedList'
import HeadingTitle from '@/components/HeadingTitle.vue'
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
    ControlButton,
    ControlDropdown,
    ControlSwitch,
    HeadingTitle,
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
    heading() {
      return {
        title: this.artist.name,
        subtitle: [
          {
            handler: this.openArtist,
            key: 'count.albums',
            count: this.album_count
          },
          { key: 'count.tracks', count: this.tracks.count }
        ]
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
        (grouping) => grouping.id === this.uiStore.artist_tracks_sort
      )
      options.filters = [
        (track) => !this.uiStore.hide_spotify || track.data_kind !== 'spotify'
      ]
      return this.tracks_list.group(options)
    }
  },
  methods: {
    openArtist() {
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
    },
    showDetails() {
      this.show_details_modal = true
    }
  }
}
</script>
