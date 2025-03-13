<template>
  <div>
    <content-with-heading>
      <template #heading-left>
        <heading-title :content="heading" />
      </template>
      <template #heading-right>
        <control-button
          :button="{ handler: openDetails, icon: 'dots-horizontal' }"
        />
        <control-button
          :button="{ handler: play, icon: 'shuffle', key: 'actions.shuffle' }"
        />
      </template>
      <template #content>
        <list-albums :items="albums" />
        <modal-dialog-composer
          :item="composer"
          :show="showDetailsModal"
          @close="showDetailsModal = false"
        />
      </template>
    </content-with-heading>
  </div>
</template>

<script>
import ContentWithHeading from '@/templates/ContentWithHeading.vue'
import ControlButton from '@/components/ControlButton.vue'
import { GroupedList } from '@/lib/GroupedList'
import HeadingTitle from '@/components/HeadingTitle.vue'
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
    ControlButton,
    HeadingTitle,
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
      showDetailsModal: false
    }
  },
  computed: {
    expression() {
      return `composer is "${this.composer.name}" and media_kind is music`
    },
    heading() {
      if (this.composer.name) {
        return {
          subtitle: [
            { count: this.composer.album_count, key: 'count.albums' },
            {
              count: this.composer.track_count,
              handler: this.openTracks,
              key: 'count.tracks'
            }
          ],
          title: this.composer.name
        }
      }
      return {}
    }
  },
  methods: {
    openDetails() {
      this.showDetailsModal = true
    },
    openTracks() {
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
