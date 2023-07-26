<template>
  <div class="fd-page">
    <content-with-heading>
      <template #options>
        <index-button-list :index="tracks.indexList" />
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
        <p class="title is-4" v-text="composer.name" />
      </template>
      <template #heading-right>
        <div class="buttons is-centered">
          <a
            class="button is-small is-light is-rounded"
            @click="show_composer_details_modal = true"
          >
            <mdicon class="icon" name="dots-horizontal" size="16" />
          </a>
          <a class="button is-small is-dark is-rounded" @click="play">
            <mdicon class="icon" name="shuffle" size="16" />
            <span v-text="$t('page.composer.shuffle')" />
          </a>
        </div>
      </template>
      <template #content>
        <p class="heading has-text-centered-mobile">
          <a
            class="has-text-link"
            @click="open_albums"
            v-text="
              $t('page.composer.album-count', {
                count: composer.album_count
              })
            "
          />
          <span>&nbsp;|&nbsp;</span>
          <span
            v-text="
              $t('page.composer.track-count', { count: composer.track_count })
            "
          />
        </p>
        <list-tracks :tracks="tracks" :expression="expression" />
        <modal-dialog-composer
          :show="show_composer_details_modal"
          :composer="composer"
          @close="show_composer_details_modal = false"
        />
      </template>
    </content-with-heading>
  </div>
</template>

<script>
import * as types from '@/store/mutation_types'
import ContentWithHeading from '@/templates/ContentWithHeading.vue'
import ControlDropdown from '@/components/ControlDropdown.vue'
import { GroupByList, byName, byRating } from '@/lib/GroupByList'
import IndexButtonList from '@/components/IndexButtonList.vue'
import ListTracks from '@/components/ListTracks.vue'
import ModalDialogComposer from '@/components/ModalDialogComposer.vue'
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
    vm.tracks_list = new GroupByList(response[1].data.tracks)
  }
}

export default {
  name: 'PageComposerTracks',
  components: {
    ContentWithHeading,
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
  beforeRouteUpdate(to, from, next) {
    const vm = this
    dataObject.load(to).then((response) => {
      dataObject.set(vm, response)
      next()
    })
  },

  data() {
    return {
      groupby_options: [
        {
          id: 1,
          name: this.$t('page.composer.sort-by.name'),
          options: byName('title_sort')
        },
        {
          id: 2,
          name: this.$t('page.composer.sort-by.rating'),
          options: byRating('rating', {
            direction: 'desc'
          })
        }
      ],
      composer: {},
      show_composer_details_modal: false,
      tracks_list: new GroupByList()
    }
  },

  computed: {
    expression() {
      return 'composer is "' + this.composer.name + '" and media_kind is music'
    },
    selected_groupby_option_id: {
      get() {
        return this.$store.state.composer_tracks_sort
      },
      set(value) {
        this.$store.commit(types.COMPOSER_TRACKS_SORT, value)
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
    open_albums() {
      this.show_details_modal = false
      this.$router.push({
        name: 'music-composer-albums',
        params: { name: this.composer.name }
      })
    },

    play() {
      webapi.player_play_expression(this.expression, true)
    }
  }
}
</script>

<style></style>
