<template>
  <div class="fd-page">
    <content-with-heading>
      <template #options>
        <index-button-list :index="tracks.indexList" />
        <div class="columns">
          <div class="column">
            <p class="heading mb-5" v-text="$t('page.genre.sort-by.title')" />
            <control-dropdown
              v-model:value="selected_groupby_option_id"
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
          :media_kind="media_kind"
          @close="show_genre_details_modal = false"
        />
      </template>
    </content-with-heading>
  </div>
</template>

<script>
import * as types from '@/store/mutation_types'
import { GroupByList, byName, byRating } from '@/lib/GroupByList'
import ContentWithHeading from '@/templates/ContentWithHeading.vue'
import ControlDropdown from '@/components/ControlDropdown.vue'
import IndexButtonList from '@/components/IndexButtonList.vue'
import ListTracks from '@/components/ListTracks.vue'
import ModalDialogGenre from '@/components/ModalDialogGenre.vue'
import webapi from '@/webapi'

const dataObject = {
  load(to) {
    return Promise.all([
      webapi.library_genre(to.params.name, to.query.media_kind),
      webapi.library_genre_tracks(to.params.name, to.query.media_kind)
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
    ControlDropdown,
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
    if (!this.tracks_list.isEmpty()) {
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
      media_kind: this.$route.query.media_kind,
      show_genre_details_modal: false,
      tracks_list: new GroupByList()
    }
  },

  computed: {
    expression() {
      return `genre is "${this.genre.name}" and media_kind is ${this.media_kind}`
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
      this.$router.push({
        name: 'genre-albums',
        params: { name: this.genre.name },
        query: { media_kind: this.media_kind }
      })
    },

    play() {
      webapi.player_play_expression(this.expression, true)
    }
  }
}
</script>

<style></style>
