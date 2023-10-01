<template>
  <div>
    <transition name="fade">
      <div v-if="show" class="modal is-active">
        <div class="modal-background" @click="$emit('close')" />
        <div class="modal-content fd-modal-card">
          <div class="card">
            <div class="card-content">
              <p class="title is-4">
                <a
                  class="has-text-link"
                  @click="open_playlist"
                  v-text="playlist.name"
                />
              </p>
              <div class="content is-small">
                <p>
                  <span
                    class="heading"
                    v-text="$t('dialog.spotify.playlist.owner')"
                  />
                  <span
                    class="title is-6"
                    v-text="playlist.owner.display_name"
                  />
                </p>
                <p>
                  <span
                    class="heading"
                    v-text="$t('dialog.spotify.playlist.tracks')"
                  />
                  <span class="title is-6" v-text="playlist.tracks.total" />
                </p>
                <p>
                  <span
                    class="heading"
                    v-text="$t('dialog.spotify.playlist.path')"
                  />
                  <span class="title is-6" v-text="playlist.uri" />
                </p>
              </div>
            </div>
            <footer class="card-footer">
              <a class="card-footer-item has-text-dark" @click="queue_add">
                <mdicon class="icon" name="playlist-plus" size="16" />
                <span
                  class="is-size-7"
                  v-text="$t('dialog.spotify.playlist.add')"
                />
              </a>
              <a class="card-footer-item has-text-dark" @click="queue_add_next">
                <mdicon class="icon" name="playlist-play" size="16" />
                <span
                  class="is-size-7"
                  v-text="$t('dialog.spotify.playlist.add-next')"
                />
              </a>
              <a class="card-footer-item has-text-dark" @click="play">
                <mdicon class="icon" name="play" size="16" />
                <span
                  class="is-size-7"
                  v-text="$t('dialog.spotify.playlist.play')"
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
  </div>
</template>

<script>
import webapi from '@/webapi'

export default {
  name: 'ModalDialogPlaylistSpotify',
  props: ['show', 'playlist'],
  emits: ['close'],

  methods: {
    play() {
      this.$emit('close')
      webapi.player_play_uri(this.playlist.uri, false)
    },

    queue_add() {
      this.$emit('close')
      webapi.queue_add(this.playlist.uri)
    },

    queue_add_next() {
      this.$emit('close')
      webapi.queue_add_next(this.playlist.uri)
    },

    open_playlist() {
      this.$emit('close')
      this.$router.push({
        name: 'playlist-spotify',
        params: { id: this.playlist.id }
      })
    }
  }
}
</script>

<style></style>
