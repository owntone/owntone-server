<template>
  <modal-dialog-action
    :actions="actions"
    :show="show"
    @add="queue_add"
    @add-next="queue_add_next"
    @close="$emit('close')"
    @play="play"
  >
    <template #modal-content>
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
  </modal-dialog-action>
</template>

<script>
import ModalDialogAction from '@/components/ModalDialogAction.vue'
import webapi from '@/webapi'

export default {
  name: 'ModalDialogTrackSpotify',
  components: { ModalDialogAction },
  props: { item: { required: true, type: Object }, show: Boolean },
  emits: ['close'],
  computed: {
    actions() {
      return [
        {
          label: this.$t('dialog.spotify.track.add'),
          event: 'add',
          icon: 'playlist-plus'
        },
        {
          label: this.$t('dialog.spotify.track.add-next'),
          event: 'add-next',
          icon: 'playlist-play'
        },
        {
          label: this.$t('dialog.spotify.track.play'),
          event: 'play',
          icon: 'play'
        }
      ]
    }
  },
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
