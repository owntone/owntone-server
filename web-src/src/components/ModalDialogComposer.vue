<template>
  <modal-dialog-playable
    :item="playable"
    :show="show"
    @close="$emit('close')"
  />
</template>

<script>
import ModalDialogPlayable from './ModalDialogPlayable.vue'

export default {
  name: 'ModalDialogComposer',
  components: { ModalDialogPlayable },
  props: { item: { required: true, type: Object }, show: Boolean },
  emits: ['close'],
  computed: {
    playable() {
      return {
        action: this.open_albums,
        name: this.item.name,
        expression: `composer is "${this.item.name}" and media_kind is music`,
        properties: [
          {
            label: 'dialog.composer.albums',
            value: this.item.album_count,
            action: this.open_albums
          },
          {
            label: 'dialog.composer.tracks',
            value: this.item.track_count,
            action: this.open_tracks
          },
          {
            label: 'dialog.composer.duration',
            value: this.$filters.durationInHours(this.item.length_ms)
          }
        ]
      }
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
    }
  }
}
</script>
