<template>
  <div>
    <tabs-music></tabs-music>

    <content-with-heading>
      <template slot="options">
        <index-button-list :index="composers_list.indexList"></index-button-list>
      </template>
      <template slot="heading-left">
        <p class="title is-4">{{ heading }}</p>
        <p class="heading">{{ composers.total }} composers</p>
      </template>
      <template slot="content">
        <list-composers :composers="composers_list"></list-composers>
      </template>
    </content-with-heading>
  </div>
</template>

<script>
import { LoadDataBeforeEnterMixin } from './mixin'
import ContentWithHeading from '@/templates/ContentWithHeading'
import TabsMusic from '@/components/TabsMusic'
import IndexButtonList from '@/components/IndexButtonList'
import ListComposers from '@/components/ListComposers'
import webapi from '@/webapi'
import Composers from '@/lib/Composers'

const composersData = {
  load: function (to) {
    return webapi.library_composers()
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
  mixins: [LoadDataBeforeEnterMixin(composersData)],
  components: { ContentWithHeading, TabsMusic, IndexButtonList, ListComposers },

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
    },

    composers_list () {
      return new Composers(this.composers.items, {
        sort: 'Name',
        group: true
      })
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
