<template>
  <transition name="fade">
    <div v-if="show" class="modal is-active">
      <div class="modal-background" @click="$emit('close')" />
      <div class="modal-content">
        <div class="card">
          <div class="card-content">
            <p class="title is-4" v-text="item.name" />
            <p class="subtitle" v-text="item.artists[0].name" />
            <div class="content is-small">
              <p>
                <span
                  class="heading"
                  v-text="$t('dialog.spotify.track.album')"
                />
                <a
                  class="title is-6 has-text-link"
                  @click="open_album"
                  v-text="item.album.name"
                />
              </p>
              <p>
                <span
                  class="heading"
                  v-text="$t('dialog.spotify.track.album-artist')"
                />
                <a
                  class="title is-6 has-text-link"
                  @click="open_artist"
                  v-text="item.artists[0].name"
                />
              </p>
              <p>
                <span
                  class="heading"
                  v-text="$t('dialog.spotify.track.release-date')"
                />
                <span
                  class="title is-6"
                  v-text="$filters.date(item.album.release_date)"
                />
              </p>
              <p>
                <span
                  class="heading"
                  v-text="$t('dialog.spotify.track.position')"
                />
                <span
                  class="title is-6"
                  v-text="[item.disc_number, item.track_number].join(' / ')"
                />
              </p>
              <p>
                <span
                  class="heading"
                  v-text="$t('dialog.spotify.track.duration')"
                />
                <span
                  class="title is-6"
                  v-text="$filters.durationInHours(item.duration_ms)"
                />
              </p>
              <p>
                <span
                  class="heading"
                  v-text="$t('dialog.spotify.track.path')"
                />
                <span class="title is-6" v-text="item.uri" />
              </p>
            </div>
          </div>
          <footer class="card-footer">
            <a class="card-footer-item has-text-dark" @click="queue_add">
              <mdicon class="icon" name="playlist-plus" size="16" />
              <span class="is-size-7" v-text="$t('dialog.spotify.track.add')" />
            </a>
            <a class="card-footer-item has-text-dark" @click="queue_add_next">
              <mdicon class="icon" name="playlist-play" size="16" />
              <span
                class="is-size-7"
                v-text="$t('dialog.spotify.track.add-next')"
              />
            </a>
            <a class="card-footer-item has-text-dark" @click="play">
              <mdicon class="icon" name="play" size="16" />
              <span
                class="is-size-7"
                v-text="$t('dialog.spotify.track.play')"
              />
            </a>
          </footer>
        </div>
      </div>
      <button
        class="modal-close is-large"
        aria-label="close"
        @click="$emit('close')"
      />
    </div>
  </transition>
</template>

<script>
import webapi from '@/webapi'

export default {
  name: 'ModalDialogTrackSpotify',
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

<style></style>
