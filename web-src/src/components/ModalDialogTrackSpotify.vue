<template>
  <modal-dialog-playable :item="item" :show="show" @close="$emit('close')">
    <template #content>
      <p class="title is-4" v-text="item.name" />
      <p class="subtitle" v-text="item.artists[0].name" />
      <div class="mb-3">
        <div
          class="is-size-7 is-uppercase"
          v-text="$t('dialog.spotify.track.album')"
        />
        <div class="title is-6">
          <a @click="open_album" v-text="item.album.name" />
        </div>
      </div>
      <div class="mb-3">
        <div
          class="is-size-7 is-uppercase"
          v-text="$t('dialog.spotify.track.album-artist')"
        />
        <div class="title is-6">
          <a @click="open_artist" v-text="item.artists[0].name" />
        </div>
      </div>
      <div class="mb-3">
        <div
          class="is-size-7 is-uppercase"
          v-text="$t('dialog.spotify.track.release-date')"
        />
        <div
          class="title is-6"
          v-text="$filters.date(item.album.release_date)"
        />
      </div>
      <div class="mb-3">
        <div
          class="is-size-7 is-uppercase"
          v-text="$t('dialog.spotify.track.position')"
        />
        <div
          class="title is-6"
          v-text="[item.disc_number, item.track_number].join(' / ')"
        />
      </div>
      <div class="mb-3">
        <div
          class="is-size-7 is-uppercase"
          v-text="$t('dialog.spotify.track.duration')"
        />
        <div
          class="title is-6"
          v-text="$filters.durationInHours(item.duration_ms)"
        />
      </div>
      <div class="mb-3">
        <div
          class="is-size-7 is-uppercase"
          v-text="$t('dialog.spotify.track.path')"
        />
        <div class="title is-6" v-text="item.uri" />
      </div>
    </template>
  </modal-dialog-playable>
</template>

<script>
import ModalDialogPlayable from '@/components/ModalDialogPlayable.vue'

export default {
  name: 'ModalDialogTrackSpotify',
  components: { ModalDialogPlayable },
  props: { item: { required: true, type: Object }, show: Boolean },
  emits: ['close'],
  methods: {
    open_album() {
      this.$emit('close')
      this.$router.push({
        name: 'music-spotify-album',
        params: { id: this.item.album.id }
      })
    },
    open_artist() {
      this.$emit('close')
      this.$router.push({
        name: 'music-spotify-artist',
        params: { id: this.item.artists[0].id }
      })
    }
  }
}
</script>
