<template>
  <div>
    <content-with-heading>
      <template #options>
        <index-button-list :indices="tracks.indices" />
        <div class="columns">
          <div class="column">
            <p class="heading mb-5" v-text="$t('page.genre.sort.title')" />
            <control-dropdown
              v-model:value="selected_grouping_id"
              :options="groupings"
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
            @click="show_details_modal = true"
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
        <list-tracks :items="tracks" :expression="expression" />
        <modal-dialog-genre
          :item="genre"
          :media_kind="media_kind"
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
    vm.genre = response[0].data.genres.items.shift()
    vm.tracks_list = new GroupedList(response[1].data.tracks)
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

  data() {
    return {
      genre: {},
      groupings: [
        {
          id: 1,
          name: this.$t('page.genre.sort.name'),
          options: { index: { field: 'title_sort', type: String } }
        },
        {
          id: 2,
          name: this.$t('page.genre.sort.rating'),
          options: {
            criteria: [{ field: 'rating', order: -1, type: Number }],
            index: { field: 'rating', type: 'Digits' }
          }
        }
      ],
      media_kind: this.$route.query.media_kind,
      show_details_modal: false,
      tracks_list: new GroupedList()
    }
  },

  computed: {
    expression() {
      return `genre is "${this.genre.name}" and media_kind is ${this.media_kind}`
    },
    selected_grouping_id: {
      get() {
        return this.$store.state.genre_tracks_sort
      },
      set(value) {
        this.$store.commit(types.GENRE_TRACKS_SORT, value)
      }
    },
    tracks() {
      const grouping = this.groupings.find(
        (grouping) => grouping.id === this.selected_grouping_id
      )
      this.tracks_list.group(grouping.options)
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
