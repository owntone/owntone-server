<template>
  <content-with-heading>
    <template #options>
      <div class="columns">
        <div class="column">
          <p class="heading" style="margin-bottom: 24px">Sort by</p>
          <dropdown-menu
            v-model="selected_groupby_option_name"
            :options="groupby_option_names"
          />
        </div>
      </div>
    </template>
    <template #heading-left>
      <p class="title is-4">
        {{ artist.name }}
      </p>
    </template>
    <template #heading-right>
      <div class="buttons is-centered">
        <a
          class="button is-small is-light is-rounded"
          @click="show_artist_details_modal = true"
        >
          <span class="icon"
            ><i class="mdi mdi-dots-horizontal mdi-18px"
          /></span>
        </a>
        <a class="button is-small is-dark is-rounded" @click="play">
          <span class="icon"><i class="mdi mdi-shuffle" /></span>
          <span>Shuffle</span>
        </a>
      </div>
    </template>
    <template #content>
      <p class="heading has-text-centered-mobile">
        {{ artist.album_count }} albums |
        <a class="has-text-link" @click="open_tracks"
          >{{ artist.track_count }} tracks</a
        >
      </p>
      <list-albums :albums="albums" :hide_group_title="true" />
      <modal-dialog-artist
        :show="show_artist_details_modal"
        :artist="artist"
        @close="show_artist_details_modal = false"
      />
    </template>
  </content-with-heading>
</template>

<script>
import ContentWithHeading from '@/templates/ContentWithHeading.vue'
import ListAlbums from '@/components/ListAlbums.vue'
import ModalDialogArtist from '@/components/ModalDialogArtist.vue'
import DropdownMenu from '@/components/DropdownMenu.vue'
import webapi from '@/webapi'
import * as types from '@/store/mutation_types'
import { bySortName, byYear, GroupByList } from '@/lib/GroupByList'

const dataObject = {
  load: function (to) {
    return Promise.all([
      webapi.library_artist(to.params.artist_id),
      webapi.library_artist_albums(to.params.artist_id)
    ])
  },

  set: function (vm, response) {
    vm.artist = response[0].data
    vm.albums_list = new GroupByList(response[1].data)
  }
}

export default {
  name: 'PageArtist',
  components: {
    ContentWithHeading,
    ListAlbums,
    ModalDialogArtist,
    DropdownMenu
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

      // List of group by/sort options for itemsGroupByList
      groupby_options: [
        { name: 'Name', options: bySortName('name_sort') },
        {
          name: 'Release date',
          options: byYear('date_released', {
            direction: 'asc',
            defaultValue: '0000'
          })
        }
      ],

      show_artist_details_modal: false
    }
  },

  computed: {
    albums() {
      const groupBy = this.groupby_options.find(
        (o) => o.name === this.selected_groupby_option_name
      )
      this.albums_list.group(groupBy.options)

      return this.albums_list
    },

    groupby_option_names() {
      return [...this.groupby_options].map((o) => o.name)
    },

    selected_groupby_option_name: {
      get() {
        return this.$store.state.artist_albums_sort
      },
      set(value) {
        this.$store.commit(types.ARTIST_ALBUMS_SORT, value)
      }
    }
  },

  methods: {
    open_tracks: function () {
      this.$router.push({
        path: '/music/artists/' + this.artist.id + '/tracks'
      })
    },

    play: function () {
      webapi.player_play_uri(
        this.albums.items.map((a) => a.uri).join(','),
        true
      )
    }
  }
}
</script>

<style></style>
