<template>
  <div>
    <content-with-heading>
      <template #options>
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
          <span
            v-text="$t('page.artist.album-count', { count: albums.count })"
          />
          <span>&nbsp;|&nbsp;</span>
          <a
            class="has-text-link"
            @click="open_tracks"
            v-text="$t('page.artist.track-count', { count: track_count })"
          />
        </p>
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
import * as types from '@/store/mutation_types'
import ContentWithHeading from '@/templates/ContentWithHeading.vue'
import ControlDropdown from '@/components/ControlDropdown.vue'
import { GroupedList } from '@/lib/GroupedList'
import ListAlbums from '@/components/ListAlbums.vue'
import ModalDialogArtist from '@/components/ModalDialogArtist.vue'
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
    ControlDropdown,
    ListAlbums,
    ModalDialogArtist
  },

  beforeRouteEnter(to, from, next) {
    dataObject.load(to).then((response) => {
      next((vm) => dataObject.set(vm, response))
    })
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
        (grouping) => grouping.id === this.selected_grouping_id
      )
      options.filters = [
        (album) => !this.hide_spotify || album.data_kind !== 'spotify'
      ]
      return this.albums_list.group(options)
    },
    hide_spotify: {
      get() {
        return this.$store.state.hide_spotify
      },
      set(value) {
        this.$store.commit(types.HIDE_SPOTIFY, value)
      }
    },
    selected_grouping_id: {
      get() {
        return this.$store.state.artist_albums_sort
      },
      set(value) {
        this.$store.commit(types.ARTIST_ALBUMS_SORT, value)
      }
    },
    spotify_enabled() {
      return this.$store.state.spotify.webapi_token_valid
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
    }
  }
}
</script>

<style></style>
