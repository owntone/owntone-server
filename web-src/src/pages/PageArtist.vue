<template>
  <div class="fd-page">
    <content-with-heading>
      <template #options>
        <div class="columns">
          <div class="column">
            <p class="heading mb-5" v-text="$t('page.artist.sort-by.title')" />
            <control-dropdown
              v-model:value="selected_groupby_option_id"
              :options="groupby_options"
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
            @click="show_artist_details_modal = true"
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
            v-text="
              $t('page.artist.album-count', { count: artist.album_count })
            "
          />
          <span>&nbsp;|&nbsp;</span>
          <a
            class="has-text-link"
            @click="open_tracks"
            v-text="
              $t('page.artist.track-count', { count: artist.track_count })
            "
          />
        </p>
        <list-albums :albums="albums" :hide_group_title="true" />
        <modal-dialog-artist
          :show="show_artist_details_modal"
          :artist="artist"
          @close="show_artist_details_modal = false"
        />
      </template>
    </content-with-heading>
  </div>
</template>

<script>
import * as types from '@/store/mutation_types'
import ContentWithHeading from '@/templates/ContentWithHeading.vue'
import ControlDropdown from '@/components/ControlDropdown.vue'
import { GroupByList, byName, byYear } from '@/lib/GroupByList'
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
    vm.albums_list = new GroupByList(response[1].data)
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
      albums_list: new GroupByList(),
      groupby_options: [
        {
          id: 1,
          name: this.$t('page.artist.sort-by.name'),
          options: byName('name_sort', true)
        },
        {
          id: 2,
          name: this.$t('page.artist.sort-by.release-date'),
          options: byYear('date_released', {
            direction: 'asc'
          })
        }
      ],
      show_artist_details_modal: false
    }
  },

  computed: {
    albums() {
      const groupBy = this.groupby_options.find(
        (o) => o.id === this.selected_groupby_option_id
      )
      this.albums_list.group(groupBy.options)

      return this.albums_list
    },

    selected_groupby_option_id: {
      get() {
        return this.$store.state.artist_albums_sort
      },
      set(value) {
        this.$store.commit(types.ARTIST_ALBUMS_SORT, value)
      }
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
        this.albums.items.map((a) => a.uri).join(','),
        true
      )
    }
  }
}
</script>

<style></style>
