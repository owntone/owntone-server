<template>
  <div>
    <transition name="fade">
      <div v-if="show" class="modal is-active">
        <div class="modal-background" @click="$emit('close')" />
        <div class="modal-content fd-modal-card">
          <div class="card">
            <div class="card-content">
              <p class="title is-4">
                <a class="has-text-link" @click="open_genre" v-text="genre.name" />
              </p>
              <div class="content is-small">
                <p>
                  <span class="heading" v-text="$t('dialog.genre.albums')" />
                  <span class="title is-6" v-text="genre.album_count" />
                </p>
                <p>
                  <span class="heading" v-text="$t('dialog.genre.tracks')" />
                  <span class="title is-6" v-text="genre.track_count" />
                </p>
                <p>
                  <span class="heading" v-text="$t('dialog.genre.duration')" />
                  <span class="title is-6" v-text="$filters.durationInHours(genre.length_ms)" />
                </p>
              </div>
            </div>
            <footer class="card-footer">
              <a class="card-footer-item has-text-dark" @click="queue_add">
                <mdicon class="icon" name="playlist-plus" size="16" />
                <span class="is-size-7" v-text="$t('dialog.genre.add')" />
              </a>
              <a class="card-footer-item has-text-dark" @click="queue_add_next">
                <mdicon class="icon" name="playlist-play" size="16" />
                <span class="is-size-7" v-text="$t('dialog.genre.add-next')" />
              </a>
              <a class="card-footer-item has-text-dark" @click="play">
                <mdicon class="icon" name="play" size="16" />
                <span class="is-size-7" v-text="$t('dialog.genre.play')" />
              </a>
            </footer>
          </div>
        </div>
        <button class="modal-close is-large" aria-label="close" @click="$emit('close')" />
      </div>
    </transition>
  </div>
</template>

<script>
import webapi from '@/webapi'

export default {
  name: 'ModalDialogGenre',
  props: ['show', 'genre'],
  emits: ['close'],

  methods: {
    play: function () {
      this.$emit('close')
      webapi.player_play_expression(
        'genre is "' + this.genre.name + '" and media_kind is music',
        false
      )
    },

    queue_add: function () {
      this.$emit('close')
      webapi.queue_expression_add(
        'genre is "' + this.genre.name + '" and media_kind is music'
      )
    },

    queue_add_next: function () {
      this.$emit('close')
      webapi.queue_expression_add_next(
        'genre is "' + this.genre.name + '" and media_kind is music'
      )
    },

    open_genre: function () {
      this.$emit('close')
      this.$router.push({ name: 'Genre', params: { genre: this.genre.name } })
    }
  }
}
</script>

<style></style>
