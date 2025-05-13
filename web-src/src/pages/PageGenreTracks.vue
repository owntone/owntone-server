<template>
  <content-with-heading>
    <template #options>
      <list-index-buttons :indices="tracks.indices" />
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
      <pane-title :content="heading" />
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
import ListIndexButtons from '@/components/ListIndexButtons.vue'
import ListOptions from '@/components/ListOptions.vue'
import ListTracks from '@/components/ListTracks.vue'
import ModalDialogGenre from '@/components/ModalDialogGenre.vue'
import PaneTitle from '@/components/PaneTitle.vue'
import library from '@/api/library'
import queue from '@/api/queue'
import { useUIStore } from '@/stores/ui'

export default {
  name: 'PageGenreTracks',
  components: {
    ContentWithHeading,
    ControlButton,
    ControlDropdown,
    ListIndexButtons,
    ListOptions,
    ListTracks,
    ModalDialogGenre,
    PaneTitle
  },
  beforeRouteEnter(to, from, next) {
    Promise.all([
      library.genre(to.params.name, to.query.mediaKind),
      library.genreTracks(to.params.name, to.query.mediaKind)
    ]).then(([genre, tracks]) => {
      next((vm) => {
        vm.genre = genre.items.shift()
        vm.trackList = new GroupedList(tracks)
      })
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
              key: 'data.albums'
            },
            { count: this.genre.track_count, key: 'data.tracks' }
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
        query: { mediaKind: this.mediaKind }
      })
    },
    play() {
      queue.playExpression(this.expression, true)
    },
    openDetails() {
      this.showDetailsModal = true
    }
  }
}
</script>
