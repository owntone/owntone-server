<template>
  <div>
    <content-with-heading>
      <template #options>
        <list-options>
          <template #filter>
            <control-switch
              v-if="servicesStore.isSpotifyActive"
              v-model="uiStore.hideSpotify"
            >
              <template #label>
                <span v-text="$t('page.artist.filter.hide-spotify')" />
              </template>
              <template #help>
                <span v-text="$t('page.artist.filter.hide-spotify-help')" />
              </template>
            </control-switch>
          </template>
          <template #sort>
            <control-dropdown
              v-model:value="uiStore.artistAlbumsSort"
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
        <list-albums :items="albums" />
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
import ListAlbums from '@/components/ListAlbums.vue'
import ListOptions from '@/components/ListOptions.vue'
import ModalDialogArtist from '@/components/ModalDialogArtist.vue'
import { useServicesStore } from '@/stores/services'
import { useUIStore } from '@/stores/ui'
import webapi from '@/webapi'

const dataObject = {
  load(to) {
    return Promise.all([
      webapi.library_artist(to.params.id),
      webapi.library_artist_albums(to.params.id)
    ])
  },
  set(vm, response) {
    vm.artist = response[0].data
    vm.albumList = new GroupedList(response[1].data)
  }
}

export default {
  name: 'PageArtist',
  components: {
    ContentWithHeading,
    ControlButton,
    ControlDropdown,
    ControlSwitch,
    HeadingTitle,
    ListAlbums,
    ListOptions,
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
      albumList: new GroupedList(),
      artist: {},
      showDetailsModal: false
    }
  },
  computed: {
    albums() {
      const { options } = this.groupings.find(
        (grouping) => grouping.id === this.uiStore.artistAlbumsSort
      )
      options.filters = [
        (album) => !this.uiStore.hideSpotify || album.data_kind !== 'spotify'
      ]
      return this.albumList.group(options)
    },
    groupings() {
      return [
        {
          id: 1,
          name: this.$t('page.artist.sort.name'),
          options: { criteria: [{ field: 'name_sort', type: String }] }
        },
        {
          id: 2,
          name: this.$t('page.artist.sort.release-date'),
          options: { criteria: [{ field: 'date_released', type: Date }] }
        }
      ]
    },
    heading() {
      return {
        subtitle: [
          { count: this.albums.count, key: 'count.albums' },
          {
            count: this.trackCount,
            handler: this.openTracks,
            key: 'count.tracks'
          }
        ],
        title: this.artist.name
      }
    },
    trackCount() {
      // The count of tracks is incorrect when albums have Spotify tracks.
      return [...this.albums].reduce(
        (total, album) => total + (album.isItem ? album.item.track_count : 0),
        0
      )
    }
  },
  methods: {
    openDetails() {
      this.showDetailsModal = true
    },
    openTracks() {
      this.$router.push({
        name: 'music-artist-tracks',
        params: { id: this.artist.id }
      })
    },
    play() {
      webapi.player_play_uri(
        this.albums.items.map((item) => item.uri).join(),
        true
      )
    }
  }
}
</script>
