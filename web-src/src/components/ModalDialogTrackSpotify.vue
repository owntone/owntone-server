<template>
  <modal-dialog :show="show" @close="$emit('close')">
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
    <template #footer>
      <a class="card-footer-item has-text-dark" @click="queue_add">
        <mdicon class="icon" name="playlist-plus" size="16" />
        <span class="is-size-7" v-text="$t('dialog.spotify.track.add')" />
      </a>
      <a class="card-footer-item has-text-dark" @click="queue_add_next">
        <mdicon class="icon" name="playlist-play" size="16" />
        <span class="is-size-7" v-text="$t('dialog.spotify.track.add-next')" />
      </a>
      <a class="card-footer-item has-text-dark" @click="play">
        <mdicon class="icon" name="play" size="16" />
        <span class="is-size-7" v-text="$t('dialog.spotify.track.play')" />
      </a>
    </template>
  </modal-dialog>
</template>

<script>
import ModalDialog from '@/components/ModalDialog.vue'
import webapi from '@/webapi'

export default {
  name: 'ModalDialogTrackSpotify',
  components: { ModalDialog },
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
