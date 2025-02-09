<template>
  <modal-dialog-playable :item="item" :show="show" @close="$emit('close')">
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
  </modal-dialog-playable>
</template>

<script>
import ModalDialogPlayable from '@/components/ModalDialogPlayable.vue'

export default {
  name: 'ModalDialogPlaylistSpotify',
  components: { ModalDialogPlayable },
  props: { item: { required: true, type: Object }, show: Boolean },
  emits: ['close'],
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
