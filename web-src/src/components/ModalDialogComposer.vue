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
                  @click="open_albums"
                  v-text="composer.name"
                />
              </p>
              <p>
                <span class="heading" v-text="$t('dialog.composer.albums')" />
                <a
                  class="has-text-link is-6"
                  @click="open_albums"
                  v-text="composer.album_count"
                />
              </p>
              <p>
                <span class="heading" v-text="$t('dialog.composer.tracks')" />
                <a
                  class="has-text-link is-6"
                  @click="open_tracks"
                  v-text="composer.track_count"
                />
              </p>
              <p>
                <span class="heading" v-text="$t('dialog.composer.duration')" />
                <span
                  class="title is-6"
                  v-text="$filters.durationInHours(composer.length_ms)"
                />
              </p>
            </div>
            <footer class="card-footer">
              <a class="card-footer-item has-text-dark" @click="queue_add">
                <mdicon class="icon" name="playlist-plus" size="16" />
                <span class="is-size-7" v-text="$t('dialog.composer.add')" />
              </a>
              <a class="card-footer-item has-text-dark" @click="queue_add_next">
                <mdicon class="icon" name="playlist-play" size="16" />
                <span
                  class="is-size-7"
                  v-text="$t('dialog.composer.add-next')"
                />
              </a>
              <a class="card-footer-item has-text-dark" @click="play">
                <mdicon class="icon" name="play" size="16" />
                <span class="is-size-7" v-text="$t('dialog.composer.play')" />
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
    play() {
      this.$emit('close')
      webapi.player_play_expression(
        'composer is "' + this.composer.name + '" and media_kind is music',
        false
      )
    },

    queue_add() {
      this.$emit('close')
      webapi.queue_expression_add(
        'composer is "' + this.composer.name + '" and media_kind is music'
      )
    },

    queue_add_next() {
      this.$emit('close')
      webapi.queue_expression_add_next(
        'composer is "' + this.composer.name + '" and media_kind is music'
      )
    },

    open_albums() {
      this.$emit('close')
      this.$router.push({
        name: 'music-composer-albums',
        params: { name: this.composer.name }
      })
    },

    open_tracks() {
      this.show_details_modal = false
      this.$router.push({
        name: 'music-composer-tracks',
        params: { name: this.composer.name }
      })
    }
  }
}
</script>

<style></style>
