<template>
  <modal-dialog-action
    :actions="actions"
    :show="show"
    @add="queue_add"
    @add-next="queue_add_next"
    @close="$emit('close')"
    @play="play"
  >
    <template #modal-content>
      <div class="title is-4">
        <a @click="open" v-text="item.name" />
      </div>
      <div class="mb-3">
        <div
          class="is-size-7 is-uppercase"
          v-text="$t('dialog.spotify.playlist.owner')"
        />
        <div class="title is-6" v-text="item.owner.display_name" />
      </div>
      <div class="mb-3">
        <div
          class="is-size-7 is-uppercase"
          v-text="$t('dialog.spotify.playlist.tracks')"
        />
        <div class="title is-6" v-text="item.tracks.total" />
      </div>
      <div class="mb-3">
        <div
          class="is-size-7 is-uppercase"
          v-text="$t('dialog.spotify.playlist.path')"
        />
        <div class="title is-6" v-text="item.uri" />
      </div>
    </template>
  </modal-dialog-action>
</template>

<script>
import ModalDialogAction from '@/components/ModalDialogAction.vue'
import webapi from '@/webapi'

export default {
  name: 'ModalDialogPlaylistSpotify',
  components: { ModalDialogAction },
  props: { item: { required: true, type: Object }, show: Boolean },
  emits: ['close'],
  computed: {
    actions() {
      return [
        {
          label: this.$t('dialog.spotify.playlist.add'),
          event: 'add',
          icon: 'playlist-plus'
        },
        {
          label: this.$t('dialog.spotify.playlist.add-next'),
          event: 'add-next',
          icon: 'playlist-play'
        },
        {
          label: this.$t('dialog.spotify.playlist.play'),
          event: 'play',
          icon: 'play'
        }
      ]
    }
  },
  methods: {
    open() {
      this.$emit('close')
      this.$router.push({
        name: 'playlist-spotify',
        params: { id: this.item.id }
      })
    },
    play() {
      this.$emit('close')
      webapi.player_play_uri(this.item.uri, false)
    },
    queue_add() {
      this.$emit('close')
      webapi.queue_add(this.item.uri)
    },
    queue_add_next() {
      this.$emit('close')
      webapi.queue_add_next(this.item.uri)
    }
  }
}
</script>
