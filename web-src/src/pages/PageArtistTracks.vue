<template>
  <div>
    <content-with-heading>
      <template #options>
        <index-button-list :indices="tracks.indices" />
        <div class="columns">
          <div class="column">
            <p
              class="is-size-7 is-uppercase"
              v-text="$t('options.filter.title')"
            />
            <control-switch
              v-if="servicesStore.isSpotifyEnabled"
              v-model="uiStore.hideSpotify"
            >
              <template #label>
                <span v-text="$t('options.filter.hide-spotify')" />
              </template>
              <template #help>
                <span v-text="$t('options.filter.hide-spotify-help')" />
              </template>
            </control-switch>
          </div>
          <div class="column">
            <p
              class="is-size-7 is-uppercase"
              v-text="$t('options.sort.title')"
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
          :button="{ handler: play, icon: 'shuffle', key: 'actions.shuffle' }"
        />
      </template>
      <template #content>
        <list-tracks :items="tracks" :uris="track_uris" />
        <modal-dialog-artist
          :item="artist"
          :show="showDetailsModal"
          @close="showDetailsModal = false"
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
    vm.trackList = new GroupedList(response[1].data.tracks)
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
      showDetailsModal: false,
      trackList: new GroupedList()
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
    groupings() {
      return [
        {
          id: 1,
          name: this.$t('options.sort.name'),
          options: { index: { field: 'title_sort', type: String } }
        },
        {
          id: 2,
          name: this.$t('options.sort.rating'),
          options: {
            criteria: [{ field: 'rating', order: -1, type: Number }],
            index: { field: 'rating', type: 'Digits' }
          }
        }
      ]
    },
    heading() {
      return {
        subtitle: [
          {
            count: this.album_count,
            handler: this.openArtist,
            key: 'count.albums'
          },
          { count: this.tracks.count, key: 'count.tracks' }
        ],
        title: this.artist.name
      }
    },
    track_uris() {
      return this.trackList.items.map((item) => item.uri).join()
    },
    tracks() {
      const { options } = this.groupings.find(
        (grouping) => grouping.id === this.uiStore.artist_tracks_sort
      )
      options.filters = [
        (track) => !this.uiStore.hideSpotify || track.data_kind !== 'spotify'
      ]
      return this.trackList.group(options)
    }
  },
  methods: {
    openArtist() {
      this.showDetailsModal = false
      this.$router.push({
        name: 'music-artist',
        params: { id: this.artist.id }
      })
    },
    play() {
      webapi.player_play_uri(
        this.trackList.items.map((item) => item.uri).join(),
        true
      )
    },
    showDetails() {
      this.showDetailsModal = true
    }
  }
}
</script>
