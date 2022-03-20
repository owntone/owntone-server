<template>
  <div>
    <transition name="fade">
      <div v-if="show" class="modal is-active">
        <div class="modal-background" @click="$emit('close')" />
        <div class="modal-content fd-modal-card">
          <div class="card">
            <div class="card-content">
              <p class="title is-4">
                {{ directory.path }}
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
  name: 'ModalDialogDirectory',
  props: ['show', 'directory'],
  emits: ['close'],

  methods: {
    play: function () {
      this.$emit('close')
      webapi.player_play_expression(
        'path starts with "' + this.directory.path + '" order by path asc',
        false
      )
    },

    queue_add: function () {
      this.$emit('close')
      webapi.queue_expression_add(
        'path starts with "' + this.directory.path + '" order by path asc'
      )
    },

    queue_add_next: function () {
      this.$emit('close')
      webapi.queue_expression_add_next(
        'path starts with "' + this.directory.path + '" order by path asc'
      )
    }
  }
}
</script>

<style></style>
