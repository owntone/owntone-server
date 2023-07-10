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
                  @click="open_artist"
                  v-text="artist.name"
                />
              </p>
              <div class="content is-small">
                <p>
                  <span class="heading" v-text="$t('dialog.artist.albums')" />
                  <span class="title is-6" v-text="artist.album_count" />
                </p>
                <p>
                  <span class="heading" v-text="$t('dialog.artist.tracks')" />
                  <span class="title is-6" v-text="artist.track_count" />
                </p>
                <p>
                  <span class="heading" v-text="$t('dialog.artist.type')" />
                  <span
                    class="title is-6"
                    v-text="$t('data.kind.' + artist.data_kind)"
                  />
                </p>
                <p>
                  <span class="heading" v-text="$t('dialog.artist.added-on')" />
                  <span
                    class="title is-6"
                    v-text="$filters.datetime(artist.time_added)"
                  />
                </p>
              </div>
            </div>
            <footer class="card-footer">
              <a class="card-footer-item has-text-dark" @click="queue_add">
                <mdicon class="icon" name="playlist-plus" size="16" />
                <span class="is-size-7" v-text="$t('dialog.artist.add')" />
              </a>
              <a class="card-footer-item has-text-dark" @click="queue_add_next">
                <mdicon class="icon" name="playlist-play" size="16" />
                <span class="is-size-7" v-text="$t('dialog.artist.add-next')" />
              </a>
              <a class="card-footer-item has-text-dark" @click="play">
                <mdicon class="icon" name="play" size="16" />
                <span class="is-size-7" v-text="$t('dialog.artist.play')" />
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
  name: 'ModalDialogArtist',
  props: ['show', 'artist'],
  emits: ['close'],

  methods: {
    play() {
      this.$emit('close')
      webapi.player_play_uri(this.artist.uri, false)
    },

    queue_add() {
      this.$emit('close')
      webapi.queue_add(this.artist.uri)
    },

    queue_add_next() {
      this.$emit('close')
      webapi.queue_add_next(this.artist.uri)
    },

    open_artist() {
      this.$emit('close')
      this.$router.push({
        name: 'music-artist',
        params: { id: this.artist.id }
      })
    }
  }
}
</script>

<style></style>
