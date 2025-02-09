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
  name: 'ModalDialogArtistSpotify',
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
            label: 'dialog.spotify.artist.popularity',
            value: [this.item.popularity, this.item.followers?.total].join(
              ' / '
            )
          },
          {
            label: 'dialog.spotify.artist.genres',
            value: this.item.genres?.join(', ')
          }
        ]
      }
    }
  },
  methods: {
    open() {
      this.$emit('close')
      this.$router.push({
        name: 'music-spotify-artist',
        params: { id: this.item.id }
      })
    }
  }
}
</script>
