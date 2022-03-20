<template>
  <div>
    <transition name="fade">
      <div v-if="show" class="modal is-active">
        <div class="modal-background" @click="$emit('close')" />
        <div class="modal-content fd-modal-card">
          <div class="card">
            <div class="card-content">
              <p class="title is-4">
                <a class="has-text-link" @click="open_albums">{{
                  composer.name
                }}</a>
              </p>
              <p>
                <span class="heading">Albums</span>
                <a class="has-text-link is-6" @click="open_albums">{{
                  composer.album_count
                }}</a>
              </p>
              <p>
                <span class="heading">Tracks</span>
                <a class="has-text-link is-6" @click="open_tracks">{{
                  composer.track_count
                }}</a>
              </p>
            </div>
            <footer class="card-footer">
              <a class="card-footer-item has-text-dark" @click="queue_add">
                <span class="icon"><i class="mdi mdi-playlist-plus" /></span>
                <span class="is-size-7">Add</span>
              </a>
              <a class="card-footer-item has-text-dark" @click="queue_add_next">
                <span class="icon"><i class="mdi mdi-playlist-play" /></span>
                <span class="is-size-7">Add Next</span>
              </a>
              <a class="card-footer-item has-text-dark" @click="play">
                <span class="icon"><i class="mdi mdi-play" /></span>
                <span class="is-size-7">Play</span>
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
  name: 'ModalDialogComposer',
  props: ['show', 'composer'],
  emits: ['close'],

  methods: {
    play: function () {
      this.$emit('close')
      webapi.player_play_expression(
        'composer is "' + this.composer.name + '" and media_kind is music',
        false
      )
    },

    queue_add: function () {
      this.$emit('close')
      webapi.queue_expression_add(
        'composer is "' + this.composer.name + '" and media_kind is music'
      )
    },

    queue_add_next: function () {
      this.$emit('close')
      webapi.queue_expression_add_next(
        'composer is "' + this.composer.name + '" and media_kind is music'
      )
    },

    open_albums: function () {
      this.$emit('close')
      this.$router.push({
        name: 'ComposerAlbums',
        params: { composer: this.composer.name }
      })
    },

    open_tracks: function () {
      this.show_details_modal = false
      this.$router.push({
        name: 'ComposerTracks',
        params: { composer: this.composer.name }
      })
    }
  }
}
</script>

<style></style>
