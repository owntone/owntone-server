<template>
  <div>
    <content-with-heading>
      <template #options>
        <div class="columns">
          <div class="column">
            <div
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
            <div
              class="is-size-7 is-uppercase"
              v-text="$t('page.artist.sort.title')"
            />
            <control-dropdown
              v-model:value="uiStore.artist_albums_sort"
              :options="groupings"
            />
          </div>
        </div>
      </template>
      <template #heading-left>
        <div class="title is-4" v-text="artist.name" />
        <div class="is-size-7 is-uppercase">
          <span v-text="$t('count.albums', { count: albums.count })" />
          <span>&nbsp;|&nbsp;</span>
          <a
            @click="open_tracks"
            v-text="$t('count.tracks', { count: track_count })"
          />
        </div>
      </template>
      <template #heading-right>
        <div class="buttons is-centered">
          <control-button :handler="showDetails" icon="dots-horizontal" />
          <control-button
            :handler="play"
            icon="shuffle"
            label="page.artist.shuffle"
          />
        </div>
      </template>
      <template #content>
        <list-albums :items="albums" />
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
import ListAlbums from '@/components/ListAlbums.vue'
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
    vm.albums_list = new GroupedList(response[1].data)
  }
}

export default {
  name: 'PageArtist',
  components: {
    ContentWithHeading,
    ControlButton,
    ControlDropdown,
    ControlSwitch,
    ListAlbums,
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
      albums_list: new GroupedList(),
      artist: {},
      groupings: [
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
      ],
      show_details_modal: false
    }
  },

  computed: {
    albums() {
      const { options } = this.groupings.find(
        (grouping) => grouping.id === this.uiStore.artist_albums_sort
      )
      options.filters = [
        (album) => !this.uiStore.hide_spotify || album.data_kind !== 'spotify'
      ]
      return this.albums_list.group(options)
    },
    spotify_enabled() {
      return this.servicesStore.spotify.webapi_token_valid
    },
    track_count() {
      // The count of tracks is incorrect when albums have Spotify tracks.
      return [...this.albums].reduce(
        (total, album) => total + (album.isItem ? album.item.track_count : 0),
        0
      )
    }
  },

  methods: {
    open_tracks() {
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
    },
    showDetails() {
      this.show_details_modal = true
    }
  }
}
</script>
