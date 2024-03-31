<template>
  <div class="fd-page-with-tabs">
    <tabs-music />
    <content-with-heading>
      <template #options>
        <index-button-list :indices="artists.indices" />
        <div class="columns">
          <div class="column">
            <p class="heading mb-5" v-text="$t('page.artists.filter')" />
            <div class="field">
              <div class="control">
                <input
                  id="switchHideSingles"
                  v-model="hide_singles"
                  type="checkbox"
                  class="switch is-rounded"
                />
                <label
                  for="switchHideSingles"
                  v-text="$t('page.artists.hide-singles')"
                />
              </div>
              <p class="help" v-text="$t('page.artists.hide-singles-help')" />
            </div>
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
                  v-text="$t('page.artists.hide-spotify')"
                />
              </div>
              <p class="help" v-text="$t('page.artists.hide-spotify-help')" />
            </div>
          </div>
          <div class="column">
            <p class="heading mb-5" v-text="$t('page.artists.sort.title')" />
            <control-dropdown
              v-model:value="selected_grouping_option_id"
              :options="grouping_options"
            />
          </div>
        </div>
      </template>
      <template #heading-left>
        <p class="title is-4" v-text="$t('page.artists.title')" />
        <p
          class="heading"
          v-text="$t('page.artists.count', { count: artists.count })"
        />
      </template>
      <template #heading-right />
      <template #content>
        <list-artists :items="artists" />
      </template>
    </content-with-heading>
  </div>
</template>

<script>
import * as types from '@/store/mutation_types'
import ContentWithHeading from '@/templates/ContentWithHeading.vue'
import ControlDropdown from '@/components/ControlDropdown.vue'
import { GroupedList } from '@/lib/GroupedList'
import IndexButtonList from '@/components/IndexButtonList.vue'
import ListArtists from '@/components/ListArtists.vue'
import TabsMusic from '@/components/TabsMusic.vue'
import webapi from '@/webapi'

const dataObject = {
  load(to) {
    return webapi.library_artists('music')
  },

  set(vm, response) {
    vm.artists_list = new GroupedList(response.data)
  }
}

export default {
  name: 'PageArtists',
  components: {
    ContentWithHeading,
    ControlDropdown,
    IndexButtonList,
    ListArtists,
    TabsMusic
  },

  beforeRouteEnter(to, from, next) {
    dataObject.load(to).then((response) => {
      next((vm) => dataObject.set(vm, response))
    })
  },

  data() {
    return {
      artists_list: new GroupedList(),
      grouping_options: [
        {
          id: 1,
          name: this.$t('page.artists.sort.name'),
          options: { index: { field: 'name_sort', type: String } }
        },
        {
          id: 2,
          name: this.$t('page.artists.sort.recently-added'),
          options: {
            criteria: [{ field: 'time_added', order: -1, type: Date }],
            index: { field: 'time_added', type: Date }
          }
        }
      ]
    }
  },

  computed: {
    // Wraps GroupedList and updates it if filter or sort changes
    artists() {
      if (!this.artists_list) {
        return []
      }
      const grouping = this.grouping_options.find(
        (o) => o.id === this.selected_grouping_option_id
      )
      grouping.options.filters = [
        (artist) =>
          !this.hide_singles || artist.track_count > artist.album_count * 2,
        (artist) => !this.hide_spotify || artist.data_kind !== 'spotify'
      ]
      this.artists_list.group(grouping.options)
      return this.artists_list
    },
    hide_singles: {
      get() {
        return this.$store.state.hide_singles
      },
      set(value) {
        this.$store.commit(types.HIDE_SINGLES, value)
      }
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
        return this.$store.state.artists_sort
      },
      set(value) {
        this.$store.commit(types.ARTISTS_SORT, value)
      }
    },
    spotify_enabled() {
      return this.$store.state.spotify.webapi_token_valid
    }
  }
}
</script>

<style></style>
