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
  name: 'ModalDialogGenre',
  components: { ModalDialogPlayable },
  props: {
    item: { required: true, type: Object },
    media_kind: { required: true, type: String },
    show: Boolean
  },
  emits: ['close'],
  computed: {
    playable() {
      return {
        name: this.item.name,
        action: this.open,
        expression: `genre is "${this.item.name}" and media_kind is ${this.media_kind}`,
        properties: [
          {
            label: 'dialog.genre.albums',
            value: this.item.album_count
          },
          {
            label: 'dialog.genre.tracks',
            value: this.item.track_count
          },
          {
            label: 'dialog.genre.duration',
            value: this.$filters.durationInHours(this.item.length_ms)
          }
        ]
      }
    }
  },
  methods: {
    open() {
      this.$emit('close')
      this.$router.push({
        name: 'genre-albums',
        params: { name: this.item.name },
        query: { media_kind: this.media_kind }
      })
    }
  }
}
</script>
