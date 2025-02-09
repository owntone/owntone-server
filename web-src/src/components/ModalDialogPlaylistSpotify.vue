<template>
  <modal-dialog :actions="actions" :show="show" @close="$emit('close')">
    <template #content>
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
  </modal-dialog>
</template>

<script>
import ModalDialog from '@/components/ModalDialog.vue'
import webapi from '@/webapi'

export default {
  name: 'ModalDialogPlaylistSpotify',
  components: { ModalDialog },
  props: { item: { required: true, type: Object }, show: Boolean },
  emits: ['close'],
  computed: {
    actions() {
      return [
        {
          label: this.$t('dialog.spotify.playlist.add'),
          handler: this.queue_add,
          icon: 'playlist-plus'
        },
        {
          label: this.$t('dialog.spotify.playlist.add-next'),
          handler: this.queue_add_next,
          icon: 'playlist-play'
        },
        {
          label: this.$t('dialog.spotify.playlist.play'),
          handler: this.play,
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
