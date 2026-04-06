<template>
  <modal-dialog-playable
    :buttons="buttons"
    :item="playable"
    :show="show"
    @close="$emit('close')"
  />
</template>

<script>
import ModalDialogPlayable from '@/components/ModalDialogPlayable.vue'
import queue from '@/api/queue'
import services from '@/api/services'

export default {
  name: 'ModalDialogArtistSpotify',
  components: { ModalDialogPlayable },
  props: { item: { required: true, type: Object }, show: Boolean },
  emits: ['close'],
  computed: {
    buttons() {
      return [
        {
          handler: this.playTopTracks,
          icon: 'play',
          key: this.$t('actions.play-top-tracks')
        }
      ]
    },
    playable() {
      return {
        image: this.item.images?.[0]?.url || '',
        name: this.item.name,
        properties: [
          {
            key: 'property.popularity',
            value: [this.item.popularity, this.item.followers?.total].join(
              ' / '
            )
          },
          { key: 'property.genres', value: this.item.genres?.join(', ') }
        ],
        uri: this.item.uri
      }
    }
  },
  methods: {
    async playTopTracks() {
      const { api, configuration } = await services.spotify.get()
      const tracks = await api.artists.topTracks(
        this.item.id,
        configuration.webapi_country
      )
      const uris = tracks.tracks.map((item) => item.uri).join(',')
      queue.playUri(uris, false)
    }
  }
}
</script>
