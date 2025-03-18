<template>
  <content-with-heading>
    <template #options>
      <index-button-list :indices="tracks.indices" />
      <list-options>
        <template #filter>
          <control-switch
            v-if="servicesStore.isSpotifyActive"
            v-model="uiStore.hideSpotify"
          >
            <template #label>
              <span v-text="$t('options.filter.hide-spotify')" />
            </template>
            <template #help>
              <span v-text="$t('options.filter.hide-spotify-help')" />
            </template>
          </control-switch>
        </template>
        <template #sort>
          <control-dropdown
            v-model:value="uiStore.artistTracksSort"
            :options="groupings"
          />
        </template>
      </list-options>
    </template>
    <template #heading>
      <heading-title :content="heading" />
    </template>
    <template #actions>
      <control-button
        :button="{ handler: openDetails, icon: 'dots-horizontal' }"
      />
      <control-button
        :button="{ handler: play, icon: 'shuffle', key: 'actions.shuffle' }"
      />
    </template>
    <template #content>
      <list-tracks :items="tracks" :uris="trackUris" />
    </template>
  </content-with-heading>
  <modal-dialog-artist
    :item="artist"
    :show="showDetailsModal"
    @close="showDetailsModal = false"
  />
</template>

<script>
import ContentWithHeading from '@/templates/ContentWithHeading.vue'
import ControlButton from '@/components/ControlButton.vue'
import ControlDropdown from '@/components/ControlDropdown.vue'
import ControlSwitch from '@/components/ControlSwitch.vue'
import { GroupedList } from '@/lib/GroupedList'
import HeadingTitle from '@/components/HeadingTitle.vue'
import IndexButtonList from '@/components/IndexButtonList.vue'
import ListOptions from '@/components/ListOptions.vue'
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
    ListOptions,
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
    albumCount() {
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
            count: this.albumCount,
            handler: this.openArtist,
            key: 'count.albums'
          },
          { count: this.tracks.count, key: 'count.tracks' }
        ],
        title: this.artist.name
      }
    },
    trackUris() {
      return this.trackList.items.map((item) => item.uri).join()
    },
    tracks() {
      const { options } = this.groupings.find(
        (grouping) => grouping.id === this.uiStore.artistTracksSort
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
    openDetails() {
      this.showDetailsModal = true
    },
    play() {
      webapi.player_play_uri(
        this.trackList.items.map((item) => item.uri).join(),
        true
      )
    }
  }
}
</script>
