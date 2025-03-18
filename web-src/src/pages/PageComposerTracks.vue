<template>
  <content-with-heading>
    <template #options>
      <index-button-list :indices="tracks.indices" />
      <list-options>
        <template #sort>
          <control-dropdown
            v-model:value="uiStore.composerTracksSort"
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
  <modal-dialog-composer
    :item="composer"
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
import ModalDialogComposer from '@/components/ModalDialogComposer.vue'
import { useUIStore } from '@/stores/ui'
import webapi from '@/webapi'

const dataObject = {
  load(to) {
    return Promise.all([
      webapi.library_composer(to.params.name),
      webapi.library_composer_tracks(to.params.name)
    ])
  },
  set(vm, response) {
    vm.composer = response[0].data
    vm.trackList = new GroupedList(response[1].data.tracks)
  }
}

export default {
  name: 'PageComposerTracks',
  components: {
    ContentWithHeading,
    ControlButton,
    ControlDropdown,
    HeadingTitle,
    IndexButtonList,
    ListOptions,
    ListTracks,
    ModalDialogComposer
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
      composer: {},
      showDetailsModal: false,
      trackList: new GroupedList()
    }
  },
  computed: {
    expression() {
      return `composer is "${this.composer.name}" and media_kind is music`
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
      if (this.composer.name) {
        return {
          subtitle: [
            {
              count: this.composer.album_count,
              handler: this.openAlbums,
              key: 'count.albums'
            },
            { count: this.composer.track_count, key: 'count.tracks' }
          ],
          title: this.composer.name
        }
      }
      return {}
    },
    tracks() {
      const { options } = this.groupings.find(
        (grouping) => grouping.id === this.uiStore.composerTracksSort
      )
      return this.trackList.group(options)
    }
  },
  methods: {
    openAlbums() {
      this.$router.push({
        name: 'music-composer-albums',
        params: { name: this.composer.name }
      })
    },
    openDetails() {
      this.showDetailsModal = true
    },
    play() {
      webapi.player_play_expression(this.expression, true)
    }
  }
}
</script>
