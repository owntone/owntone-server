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
                  @click="open_genre"
                  v-text="genre.name"
                />
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
                  <span
                    class="title is-6"
                    v-text="$filters.durationInHours(genre.length_ms)"
                  />
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
  name: 'ModalDialogGenre',
  props: ['genre', 'media_kind', 'show'],
  emits: ['close'],

  computed: {
    expression() {
      return `genre is "${this.genre.name}" and media_kind is ${this.media_kind}`
    }
  },
  methods: {
    play() {
      this.$emit('close')
      webapi.player_play_expression(this.expression, false)
    },

    queue_add() {
      this.$emit('close')
      webapi.queue_expression_add(this.expression)
    },

    queue_add_next() {
      this.$emit('close')
      webapi.queue_expression_add_next(this.expression)
    },

    open_genre() {
      this.$emit('close')
      this.$router.push({
        name: 'genre-albums',
        params: { name: this.genre.name },
        query: { media_kind: this.media_kind }
      })
    }
  }
}
</script>

<style></style>
