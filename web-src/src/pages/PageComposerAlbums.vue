<template>
  <content-with-heading>
    <template #heading>
      <pane-title :content="heading" />
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
      <list-albums :items="albums" />
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
import { GroupedList } from '@/lib/GroupedList'
import ListAlbums from '@/components/ListAlbums.vue'
import ModalDialogComposer from '@/components/ModalDialogComposer.vue'
import PaneTitle from '@/components/PaneTitle.vue'
import library from '@/api/library'
import queue from '@/api/queue'

export default {
  name: 'PageComposerAlbums',
  components: {
    ContentWithHeading,
    ControlButton,
    ListAlbums,
    ModalDialogComposer,
    PaneTitle
  },
  beforeRouteEnter(to, from, next) {
    Promise.all([
      library.composer(to.params.name),
      library.composerAlbums(to.params.name)
    ]).then(([composer, albums]) => {
      next((vm) => {
        vm.composer = composer
        vm.albums = new GroupedList(albums)
      })
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
            { count: this.composer.album_count, key: 'data.albums' },
            {
              count: this.composer.track_count,
              handler: this.openTracks,
              key: 'data.tracks'
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
      queue.playExpression(this.expression, true)
    }
  }
}
</script>
