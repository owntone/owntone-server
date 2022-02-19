<template>
  <div>
    <tabs-music></tabs-music>

    <content-with-heading>
      <template v-slot:options>
        <index-button-list :index="composers_list.indexList"></index-button-list>
      </template>
      <template v-slot:heading-left>
        <p class="title is-4">{{ heading }}</p>
        <p class="heading">{{ composers.total }} composers</p>
      </template>
      <template v-slot:content>
        <list-composers :composers="composers_list"></list-composers>
      </template>
    </content-with-heading>
  </div>
</template>

<script>
import ContentWithHeading from '@/templates/ContentWithHeading.vue'
import TabsMusic from '@/components/TabsMusic.vue'
import IndexButtonList from '@/components/IndexButtonList.vue'
import ListComposers from '@/components/ListComposers.vue'
import webapi from '@/webapi'
import Composers from '@/lib/Composers'

const dataObject = {
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
  },

  beforeRouteEnter (to, from, next) {
    dataObject.load(to).then((response) => {
      next(vm => dataObject.set(vm, response))
    })
  },
  beforeRouteUpdate (to, from, next) {
    const vm = this
    dataObject.load(to).then((response) => {
      dataObject.set(vm, response)
      next()
    })
  }
}
</script>

<style>
</style>
