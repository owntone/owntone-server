<template>
  <div>
    <tabs-music></tabs-music>

    <content-with-heading>
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
import ListComposers from '@/components/ListComposers'
import webapi from '@/webapi'
import * as types from '@/store/mutation_types'
import Composers from '@/lib/Composers'

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
  mixins: [LoadDataBeforeEnterMixin(composersData)],
  components: { ContentWithHeading, TabsMusic, ListComposers },

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
        hideSingles: this.hide_singles,
        hideSpotify: this.hide_spotify,
        sort: this.sort,
        group: true
      })
    },

    hide_singles: {
      get () {
        return this.$store.state.hide_singles
      },
      set (value) {
        this.$store.commit(types.HIDE_SINGLES, value)
      }
    },

    hide_spotify: {
      get () {
        return this.$store.state.hide_spotify
      },
      set (value) {
        this.$store.commit(types.HIDE_SPOTIFY, value)
      }
    },

    sort: {
      get () {
        return 'Name'
      }
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
