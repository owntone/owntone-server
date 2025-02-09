<template>
  <modal-dialog-playable
    :item="playable"
    :show="show"
    @close="$emit('close')"
  />
</template>

<script>
import ModalDialogPlayable from '@/components/ModalDialogPlayable.vue'

export default {
  name: 'ModalDialogPlaylistSpotify',
  components: { ModalDialogPlayable },
  props: { item: { required: true, type: Object }, show: Boolean },
  emits: ['close'],
  computed: {
    playable() {
      return {
        name: this.item.name,
        action: this.open,
        properties: [
          {
            label: 'dialog.spotify.playlist.owner',
            value: this.item.owner?.display_name
          },
          {
            label: 'dialog.spotify.playlist.tracks',
            value: this.item.tracks?.total
          },
          { label: 'dialog.spotify.playlist.path', value: this.item.uri }
        ]
      }
    }
  },
  methods: {
    open() {
      this.$emit('close')
      this.$router.push({
        name: 'playlist-spotify',
        params: { id: this.item.id }
      })
    }
  }
}
</script>
