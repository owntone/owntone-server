<template>
  <div>
    <content-with-heading>
      <template #options>
        <index-button-list :indices="tracks.indices" />
        <div class="columns">
          <div class="column">
            <div
              class="is-size-7 is-uppercase"
              v-text="$t('page.artist.sort.title')"
            />
            <control-dropdown
              v-model:value="uiStore.composer_tracks_sort"
              :options="groupings"
            />
          </div>
        </div>
      </template>
      <template #heading-left>
        <div class="title is-4" v-text="composer.name" />
        <div class="is-size-7 is-uppercase">
          <a
            @click="open_albums"
            v-text="$t('count.albums', { count: composer.album_count })"
          />
          <span>&nbsp;|&nbsp;</span>
          <span v-text="$t('count.tracks', { count: composer.track_count })" />
        </div>
      </template>
      <template #heading-right>
        <div class="buttons is-centered">
          <control-button :handler="showDetails" icon="dots-horizontal" />
          <control-button
            :handler="play"
            icon="shuffle"
            label="page.composer.shuffle"
          />
        </div>
      </template>
      <template #content>
        <list-tracks :items="tracks" :expression="expression" />
        <modal-dialog-composer
          :item="composer"
          :show="show_details_modal"
          @close="show_details_modal = false"
        />
      </template>
    </content-with-heading>
  </div>
</template>

<script>
import ContentWithHeading from '@/templates/ContentWithHeading.vue'
import ControlButton from '@/components/ControlButton.vue'
import ControlDropdown from '@/components/ControlDropdown.vue'
import { GroupedList } from '@/lib/GroupedList'
import IndexButtonList from '@/components/IndexButtonList.vue'
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
    vm.tracks_list = new GroupedList(response[1].data.tracks)
  }
}

export default {
  name: 'PageComposerTracks',
  components: {
    ContentWithHeading,
    ControlButton,
    ControlDropdown,
    IndexButtonList,
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
      groupings: [
        {
          id: 1,
          name: this.$t('page.composer.sort.name'),
          options: { index: { field: 'title_sort', type: String } }
        },
        {
          id: 2,
          name: this.$t('page.composer.sort.rating'),
          options: {
            criteria: [{ field: 'rating', order: -1, type: Number }],
            index: { field: 'rating', type: 'Digits' }
          }
        }
      ],
      show_details_modal: false,
      tracks_list: new GroupedList()
    }
  },
  computed: {
    expression() {
      return `composer is "${this.composer.name}" and media_kind is music`
    },
    tracks() {
      const { options } = this.groupings.find(
        (grouping) => grouping.id === this.uiStore.composer_tracks_sort
      )
      return this.tracks_list.group(options)
    }
  },
  methods: {
    open_albums() {
      this.$router.push({
        name: 'music-composer-albums',
        params: { name: this.composer.name }
      })
    },
    play() {
      webapi.player_play_expression(this.expression, true)
    },
    showDetails() {
      this.show_details_modal = true
    }
  }
}
</script>
