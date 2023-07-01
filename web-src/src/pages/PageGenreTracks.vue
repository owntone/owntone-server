<template>
  <div class="fd-page">
    <content-with-heading>
      <template #options>
        <index-button-list :index="tracks.indexList" />
        <div class="columns">
          <div class="column">
            <p class="heading mb-5" v-text="$t('page.genre.sort-by.title')" />
            <dropdown-menu
              v-model="selected_groupby_option_id"
              :options="groupby_options"
            />
          </div>
        </div>
      </template>
      <template #heading-left>
        <p class="title is-4" v-text="genre.name" />
      </template>
      <template #heading-right>
        <div class="buttons is-centered">
          <a
            class="button is-small is-light is-rounded"
            @click="show_genre_details_modal = true"
          >
            <mdicon class="icon" name="dots-horizontal" size="16" />
          </a>
          <a class="button is-small is-dark is-rounded" @click="play">
            <mdicon class="icon" name="shuffle" size="16" />
            <span v-text="$t('page.genre.shuffle')" />
          </a>
        </div>
      </template>
      <template #content>
        <p class="heading has-text-centered-mobile">
          <a
            class="has-text-link"
            @click="open_genre"
            v-text="$t('page.genre.album-count', { count: genre.album_count })"
          />
          <span>&nbsp;|&nbsp;</span>
          <span
            v-text="$t('page.genre.track-count', { count: genre.track_count })"
          />
        </p>
        <list-tracks :tracks="tracks" :expression="expression" />
        <modal-dialog-genre
          :show="show_genre_details_modal"
          :genre="genre"
          @close="show_genre_details_modal = false"
        />
      </template>
    </content-with-heading>
  </div>
</template>

<script>
import ContentWithHeading from '@/templates/ContentWithHeading.vue'
import DropdownMenu from '@/components/DropdownMenu.vue'
import IndexButtonList from '@/components/IndexButtonList.vue'
import ListTracks from '@/components/ListTracks.vue'
import ModalDialogGenre from '@/components/ModalDialogGenre.vue'
import webapi from '@/webapi'
import * as types from '@/store/mutation_types'
import { GroupByList, byName, byRating } from '@/lib/GroupByList'

const dataObject = {
  load(to) {
    return Promise.all([
      webapi.library_genre(to.params.genre),
      webapi.library_genre_tracks(to.params.genre)
    ])
  },

  set(vm, response) {
    vm.genre = response[0].data
    vm.tracks_list = new GroupByList(response[1].data.tracks)
  }
}

export default {
  name: 'PageGenreTracks',
  components: {
    ContentWithHeading,
    DropdownMenu,
    IndexButtonList,
    ListTracks,
    ModalDialogGenre
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
      genre: {},
      groupby_options: [
        {
          id: 1,
          name: this.$t('page.genre.sort-by.name'),
          options: byName('title_sort')
        },
        {
          id: 2,
          name: this.$t('page.genre.sort-by.rating'),
          options: byRating('rating', {
            direction: 'desc'
          })
        }
      ],
      show_genre_details_modal: false,
      tracks_list: new GroupByList()
    }
  },

  computed: {
    expression() {
      return 'genre is "' + this.genre.name + '" and media_kind is music'
    },
    selected_groupby_option_id: {
      get() {
        return this.$store.state.genre_tracks_sort
      },
      set(value) {
        this.$store.commit(types.GENRE_TRACKS_SORT, value)
      }
    },
    tracks() {
      const groupBy = this.groupby_options.find(
        (o) => o.id === this.selected_groupby_option_id
      )
      this.tracks_list.group(groupBy.options)
      return this.tracks_list
    }
  },

  methods: {
    open_genre() {
      this.show_details_modal = false
      this.$router.push({ name: 'Genre', params: { genre: this.genre.name } })
    },

    play() {
      webapi.player_play_expression(this.expression, true)
    }
  }
}
</script>

<style></style>
