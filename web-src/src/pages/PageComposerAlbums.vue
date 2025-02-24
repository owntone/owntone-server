<template>
  <div>
    <content-with-heading>
      <template #heading-left>
        <div class="title is-4" v-text="composer.name" />
        <div class="is-size-7 is-uppercase">
          <span v-text="$t('count.albums', { count: composer.album_count })" />
          <span>&nbsp;|&nbsp;</span>
          <a
            @click="open_tracks"
            v-text="$t('count.tracks', { count: composer.track_count })"
          />
        </div>
      </template>
      <template #heading-right>
        <div class="buttons is-centered">
          <a
            class="button is-small is-rounded"
            @click="show_details_modal = true"
          >
            <mdicon class="icon" name="dots-horizontal" size="16" />
          </a>
          <a class="button is-small is-rounded" @click="play">
            <mdicon class="icon" name="shuffle" size="16" />
            <span v-text="$t('page.composer.shuffle')" />
          </a>
        </div>
      </template>
      <template #content>
        <list-albums :items="albums" />
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
import { GroupedList } from '@/lib/GroupedList'
import ListAlbums from '@/components/ListAlbums.vue'
import ModalDialogComposer from '@/components/ModalDialogComposer.vue'
import webapi from '@/webapi'

const dataObject = {
  load(to) {
    return Promise.all([
      webapi.library_composer(to.params.name),
      webapi.library_composer_albums(to.params.name)
    ])
  },

  set(vm, response) {
    vm.composer = response[0].data
    vm.albums = new GroupedList(response[1].data.albums)
  }
}

export default {
  name: 'PageComposerAlbums',
  components: {
    ContentWithHeading,
    ListAlbums,
    ModalDialogComposer
  },
  beforeRouteEnter(to, from, next) {
    dataObject.load(to).then((response) => {
      next((vm) => dataObject.set(vm, response))
    })
  },
  data() {
    return {
      albums: new GroupedList(),
      composer: {},
      show_details_modal: false
    }
  },
  computed: {
    expression() {
      return `composer is "${this.composer.name}" and media_kind is music`
    }
  },
  methods: {
    open_tracks() {
      this.$router.push({
        name: 'music-composer-tracks',
        params: { name: this.composer.name }
      })
    },
    play() {
      webapi.player_play_expression(this.expression, true)
    }
  }
}
</script>
