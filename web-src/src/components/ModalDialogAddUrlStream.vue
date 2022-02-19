<template>
  <div>
    <transition name="fade">
      <div v-if="show" class="modal is-active">
        <div class="modal-background" @click="$emit('close')" />
        <div class="modal-content fd-modal-card">
          <div class="card">
            <div class="card-content">
              <p class="title is-4">Add stream URL</p>
              <form class="fd-has-margin-bottom" @submit.prevent="play">
                <div class="field">
                  <p class="control is-expanded has-icons-left">
                    <input
                      ref="url_field"
                      v-model="url"
                      class="input is-shadowless"
                      type="text"
                      placeholder="http://url-to-stream"
                      :disabled="loading"
                    />
                    <span class="icon is-left">
                      <i class="mdi mdi-web" />
                    </span>
                  </p>
                </div>
              </form>
            </div>
            <footer v-if="loading" class="card-footer">
              <a class="card-footer-item has-text-dark">
                <span class="icon"><i class="mdi mdi-web" /></span>
                <span class="is-size-7">Loading ...</span>
              </a>
            </footer>
            <footer v-else class="card-footer">
              <a
                class="card-footer-item has-text-danger"
                @click="$emit('close')"
              >
                <span class="icon"><i class="mdi mdi-cancel" /></span>
                <span class="is-size-7">Cancel</span>
              </a>
              <a class="card-footer-item has-text-dark" @click="add_stream">
                <span class="icon"><i class="mdi mdi-playlist-plus" /></span>
                <span class="is-size-7">Add</span>
              </a>
              <a
                class="card-footer-item has-background-info has-text-white has-text-weight-bold"
                @click="play"
              >
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
  name: 'ModalDialogAddUrlStream',
  props: ['show'],
  emits: ['close'],

  data() {
    return {
      url: '',
      loading: false
    }
  },

  watch: {
    show() {
      if (this.show) {
        this.loading = false

        // We need to delay setting the focus to the input field until the field is part of the dom and visible
        setTimeout(() => {
          this.$refs.url_field.focus()
        }, 10)
      }
    }
  },

  methods: {
    add_stream: function () {
      this.loading = true
      webapi
        .queue_add(this.url)
        .then(() => {
          this.$emit('close')
          this.url = ''
        })
        .catch(() => {
          this.loading = false
        })
    },

    play: function () {
      this.loading = true
      webapi
        .player_play_uri(this.url, false)
        .then(() => {
          this.$emit('close')
          this.url = ''
        })
        .catch(() => {
          this.loading = false
        })
    }
  }
}
</script>

<style></style>
