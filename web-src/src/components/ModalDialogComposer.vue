<template>
  <modal-dialog
    :actions="actions"
    :show="show"
    @add="queue_add"
    @add-next="queue_add_next"
    @close="$emit('close')"
    @play="play"
  >
    <template #modal-content>
      <div class="title is-4">
        <a @click="open_albums" v-text="item.name" />
      </div>
      <div class="mb-3">
        <div
          class="is-size-7 is-uppercase"
          v-text="$t('dialog.composer.albums')"
        />
        <div class="title is-6">
          <a @click="open_albums" v-text="item.album_count" />
        </div>
      </div>
      <div class="mb-3">
        <div
          class="is-size-7 is-uppercase"
          v-text="$t('dialog.composer.tracks')"
        />
        <div class="title is-6">
          <a @click="open_tracks" v-text="item.track_count" />
        </div>
      </div>
      <div class="mb-3">
        <div
          class="is-size-7 is-uppercase"
          v-text="$t('dialog.composer.duration')"
        />
        <div
          class="title is-6"
          v-text="$filters.durationInHours(item.length_ms)"
        />
      </div>
    </template>
  </modal-dialog>
</template>

<script>
import ModalDialog from '@/components/ModalDialog.vue'
import webapi from '@/webapi'

export default {
  name: 'ModalDialogComposer',
  components: { ModalDialog },
  props: { item: { required: true, type: Object }, show: Boolean },
  emits: ['close'],
  computed: {
    actions() {
      return [
        {
          label: this.$t('dialog.composer.add'),
          event: 'add',
          icon: 'playlist-plus'
        },
        {
          label: this.$t('dialog.composer.add-next'),
          event: 'add-next',
          icon: 'playlist-play'
        },
        {
          label: this.$t('dialog.composer.play'),
          event: 'play',
          icon: 'play'
        }
      ]
    }
  },
  methods: {
    open_albums() {
      this.$emit('close')
      this.$router.push({
        name: 'music-composer-albums',
        params: { name: this.item.name }
      })
    },
    open_tracks() {
      this.$router.push({
        name: 'music-composer-tracks',
        params: { name: this.item.name }
      })
    },
    play() {
      this.$emit('close')
      webapi.player_play_expression(
        `composer is "${this.item.name}" and media_kind is music`,
        false
      )
    },
    queue_add() {
      this.$emit('close')
      webapi.queue_expression_add(
        `composer is "${this.item.name}" and media_kind is music`
      )
    },
    queue_add_next() {
      this.$emit('close')
      webapi.queue_expression_add_next(
        `composer is "${this.item.name}" and media_kind is music`
      )
    }
  }
}
</script>
