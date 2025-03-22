<template>
  <content-with-heading>
    <template #options>
      <index-button-list :indices="tracks.indices" />
      <list-options>
        <template #sort>
          <control-dropdown
            v-model:value="uiStore.genreTracksSort"
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
      <list-tracks :items="tracks" :expression="expression" />
    </template>
  </content-with-heading>
  <modal-dialog-genre
    :item="genre"
    :media-kind="mediaKind"
    :show="showDetailsModal"
    @close="showDetailsModal = false"
  />
</template>

<script>
import ContentWithHeading from '@/templates/ContentWithHeading.vue'
import ControlButton from '@/components/ControlButton.vue'
import ControlDropdown from '@/components/ControlDropdown.vue'
import { GroupedList } from '@/lib/GroupedList'
import HeadingTitle from '@/components/HeadingTitle.vue'
import IndexButtonList from '@/components/IndexButtonList.vue'
import ListOptions from '@/components/ListOptions.vue'
import ListTracks from '@/components/ListTracks.vue'
import ModalDialogGenre from '@/components/ModalDialogGenre.vue'
import { useUIStore } from '@/stores/ui'
import webapi from '@/webapi'

const dataObject = {
  load(to) {
    return Promise.all([
      webapi.library_genre(to.params.name, to.query.mediaKind),
      webapi.library_genre_tracks(to.params.name, to.query.mediaKind)
    ])
  },
  set(vm, response) {
    vm.genre = response[0].data.genres.items.shift()
    vm.trackList = new GroupedList(response[1].data.tracks)
  }
}

export default {
  name: 'PageGenreTracks',
  components: {
    ContentWithHeading,
    ControlButton,
    ControlDropdown,
    HeadingTitle,
    IndexButtonList,
    ListOptions,
    ListTracks,
    ModalDialogGenre
  },
  beforeRouteEnter(to, from, next) {
    dataObject.load(to).then((response) => {
      next((vm) => dataObject.set(vm, response))
    })
  },
  setup() {
    return { uiStore: useUIStore() }
  },
  data() {
    return {
      genre: {},
      mediaKind: this.$route.query.mediaKind,
      showDetailsModal: false,
      trackList: new GroupedList()
    }
  },
  computed: {
    expression() {
      return `genre is "${this.genre.name}" and media_kind is ${this.mediaKind}`
    },
    groupings() {
      return [
        {
          id: 1,
          name: this.$t('options.sort.name'),
          options: { index: { field: 'title_sort', type: String } }
        },
        {
          id: 2,
          name: this.$t('options.sort.rating'),
          options: {
            criteria: [{ field: 'rating', order: -1, type: Number }],
            index: { field: 'rating', type: 'Digits' }
          }
        }
      ]
    },
    heading() {
      if (this.genre.name) {
        return {
          subtitle: [
            {
              count: this.genre.album_count,
              handler: this.openGenre,
              key: 'count.albums'
            },
            { count: this.genre.track_count, key: 'count.tracks' }
          ],
          title: this.genre.name
        }
      }
      return {}
    },
    tracks() {
      const { options } = this.groupings.find(
        (grouping) => grouping.id === this.uiStore.genreTracksSort
      )
      return this.trackList.group(options)
    }
  },
  methods: {
    openGenre() {
      this.showDetailsModal = false
      this.$router.push({
        name: 'genre-albums',
        params: { name: this.genre.name },
        query: { mediaKind: this.media_kind }
      })
    },
    play() {
      webapi.player_play_expression(this.expression, true)
    },
    openDetails() {
      this.showDetailsModal = true
    }
  }
}
</script>
