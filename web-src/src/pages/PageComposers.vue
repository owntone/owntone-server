<template>
  <div>
    <tabs-music></tabs-music>

    <content-with-heading>
      <template slot="options">
        <index-button-list :index="index_list"></index-button-list>
      </template>
      <template slot="heading-left">
        <p class="title is-4">{{ heading }}</p>
        <p class="heading">{{ composers.total }} composers</p>
      </template>
      <template slot="content">
        <list-item-composer v-for="composer in composers.items" :key="composer.name" :composer="composer" @click="open_composer(composer)">
          <template slot="actions">
            <a @click="open_dialog(composer)">
              <span class="icon has-text-dark"><i class="mdi mdi-dots-vertical mdi-18px"></i></span>
            </a>
          </template>
        </list-item-composer>
        <modal-dialog-composer :show="show_details_modal" :composer="selected_composer" @close="show_details_modal = false" />
      </template>
    </content-with-heading>
  </div>
</template>

<script>
import { LoadDataBeforeEnterMixin } from './mixin'
import ContentWithHeading from '@/templates/ContentWithHeading'
import TabsMusic from '@/components/TabsMusic'
import IndexButtonList from '@/components/IndexButtonList'
import ListItemComposer from '@/components/ListItemComposer'
import ModalDialogComposer from '@/components/ModalDialogComposer'
import webapi from '@/webapi'

const composersData = {
  load: function (to) {
    if (to.params.genre) {
      return webapi.library_genre_composers(to.params.genre)
    } else {
      return webapi.library_composers()
    }
  },

  set: function (vm, response) {
    if (response.data.composers) {
      vm.composers = response.data.composers
      vm.heading = vm.$route.params.genre
    } else {
      vm.composers = response.data
      vm.heading = 'Composers'
    }
  }
}

export default {
  name: 'PageComposers',
  mixins: [ LoadDataBeforeEnterMixin(composersData) ],
  components: { ContentWithHeading, TabsMusic, ListItemComposer, IndexButtonList, ModalDialogComposer },

  data () {
    return {
      composers: { items: [] },
      heading: '',

      show_details_modal: false,
      selected_composer: {}
    }
  },

  computed: {
    index_list () {
      return [...new Set(this.composers.items
        .map(composer => composer.name.charAt(0).toUpperCase()))]
    }
  },

  methods: {
    open_composer: function (composer) {
      this.$router.push({ name: 'ComposerAlbums', params: { composer: composer.name } })
    },

    open_dialog: function (composer) {
      this.selected_composer = composer
      this.show_details_modal = true
    }
  }
}
</script>

<style>
</style>
