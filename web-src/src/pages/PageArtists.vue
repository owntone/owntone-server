<template>
  <div class="fd-page-with-tabs">
    <tabs-music />
    <content-with-heading>
      <template #options>
        <index-button-list :index="artists.indexList" />
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
            <p class="heading mb-5" v-text="$t('page.artists.sort-by.title')" />
            <control-dropdown
              v-model:value="selected_groupby_option_id"
              :options="groupby_options"
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
        <list-artists :artists="artists" />
      </template>
    </content-with-heading>
  </div>
</template>

<script>
import * as types from '@/store/mutation_types'
import { GroupByList, byName, byYear } from '@/lib/GroupByList'
import ContentWithHeading from '@/templates/ContentWithHeading.vue'
import ControlDropdown from '@/components/ControlDropdown.vue'
import IndexButtonList from '@/components/IndexButtonList.vue'
import ListArtists from '@/components/ListArtists.vue'
import TabsMusic from '@/components/TabsMusic.vue'
import webapi from '@/webapi'

const dataObject = {
  load(to) {
    return webapi.library_artists('music')
  },

  set(vm, response) {
    vm.artists_list = new GroupByList(response.data)
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

  beforeRouteUpdate(to, from, next) {
    if (!this.artists_list.isEmpty()) {
      next()
      return
    }
    const vm = this
    dataObject.load(to).then((response) => {
      dataObject.set(vm, response)
      next()
    })
  },

  data() {
    return {
      artists_list: new GroupByList(),
      groupby_options: [
        {
          id: 1,
          name: this.$t('page.artists.sort-by.name'),
          options: byName('name_sort', true)
        },
        {
          id: 2,
          name: this.$t('page.artists.sort-by.recently-added'),
          options: byYear('time_added', {
            direction: 'desc'
          })
        }
      ]
    }
  },

  computed: {
    // Wraps GroupByList and updates it if filter or sort changes
    artists() {
      if (!this.artists_list) {
        return []
      }

      const groupBy = this.groupby_options.find(
        (o) => o.id === this.selected_groupby_option_id
      )
      this.artists_list.group(groupBy.options, [
        (artist) =>
          !this.hide_singles || artist.track_count > artist.album_count * 2,
        (artist) => !this.hide_spotify || artist.data_kind !== 'spotify'
      ])

      return this.artists_list
    },

    selected_groupby_option_id: {
      get() {
        return this.$store.state.artists_sort
      },
      set(value) {
        this.$store.commit(types.ARTISTS_SORT, value)
      }
    },

    spotify_enabled() {
      return this.$store.state.spotify.webapi_token_valid
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
    }
  }
}
</script>

<style></style>
