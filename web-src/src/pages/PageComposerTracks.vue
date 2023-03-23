<template>
  <div>
    <content-with-heading>
      <template #options>
        <index-button-list :index="tracks.indexList" />
        <div class="columns">
          <div class="column">
            <p
              class="heading"
              style="margin-bottom: 24px"
              v-text="$t('page.artist.sort-by.title')"
            />
            <dropdown-menu
              v-model="selected_groupby_option_id"
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
            <span class="icon"
              ><mdicon name="dots-horizontal" size="16"
            /></span>
          </a>
          <a class="button is-small is-dark is-rounded" @click="play">
            <span class="icon"><mdicon name="shuffle" size="16" /></span>
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
import ContentWithHeading from '@/templates/ContentWithHeading.vue'
import DropdownMenu from '@/components/DropdownMenu.vue'
import IndexButtonList from '@/components/IndexButtonList.vue'
import ListTracks from '@/components/ListTracks.vue'
import ModalDialogComposer from '@/components/ModalDialogComposer.vue'
import webapi from '@/webapi'
import * as types from '@/store/mutation_types'
import { byName, byRating, GroupByList } from '@/lib/GroupByList'

const dataObject = {
  load: function (to) {
    return Promise.all([
      webapi.library_composer(to.params.composer),
      webapi.library_composer_tracks(to.params.composer)
    ])
  },

  set: function (vm, response) {
    vm.composer = response[0].data
    vm.tracks_list = new GroupByList(response[1].data.tracks)
  }
}

export default {
  name: 'PageComposerTracks',
  components: {
    ContentWithHeading,
    DropdownMenu,
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
    open_albums: function () {
      this.show_details_modal = false
      this.$router.push({
        name: 'ComposerAlbums',
        params: { composer: this.composer.name }
      })
    },

    play: function () {
      webapi.player_play_expression(this.expression, true)
    }
  }
}
</script>

<style></style>
