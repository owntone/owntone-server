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
  name: 'ModalDialogArtist',
  components: { ModalDialogPlayable },
  props: { item: { required: true, type: Object }, show: Boolean },
  emits: ['close'],
  computed: {
    playable() {
      return {
        name: this.item.name,
        action: this.open,
        properties: [
          { label: 'dialog.artist.albums', value: this.item.album_count },
          { label: 'dialog.artist.tracks', value: this.item.track_count },
          {
            label: 'dialog.artist.type',
            value: this.$t(`data.kind.${this.item.data_kind}`)
          },
          {
            label: 'dialog.artist.added-on',
            value: this.$filters.datetime(this.item.time_added)
          }
        ]
      }
    }
  },
  methods: {
    open() {
      this.$emit('close')
      this.$router.push({
        name: 'music-artist',
        params: { id: this.item.id }
      })
    }
  }
}
</script>
