<template>
  <modal-dialog-playable :item="item" :show="show" @close="$emit('close')">
    <template #content>
      <div class="title is-4">
        <a @click="open" v-text="item.name" />
      </div>
      <div class="mb-3">
        <div
          class="is-size-7 is-uppercase"
          v-text="$t('dialog.spotify.artist.popularity')"
        />
        <div
          class="title is-6"
          v-text="[item.popularity, item.followers.total].join(' / ')"
        />
      </div>
      <div class="mb-3">
        <div
          class="is-size-7 is-uppercase"
          v-text="$t('dialog.spotify.artist.genres')"
        />
        <div class="title is-6" v-text="item.genres.join(', ')" />
      </div>
    </template>
  </modal-dialog-playable>
</template>

<script>
import ModalDialogPlayable from '@/components/ModalDialogPlayable.vue'

export default {
  name: 'ModalDialogArtistSpotify',
  components: { ModalDialogPlayable },
  props: { item: { required: true, type: Object }, show: Boolean },
  emits: ['close'],
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
