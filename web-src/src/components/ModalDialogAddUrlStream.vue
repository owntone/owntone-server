<template>
  <div>
    <transition name="fade">
      <div class="modal is-active" v-if="show">
        <div class="modal-background" @click="$emit('close')"></div>
        <div class="modal-content fd-modal-card">
          <div class="card">
            <div class="card-content">
              <p class="title is-4">
                Add stream URL
              </p>
              <form v-on:submit.prevent="play" class="fd-has-margin-bottom">
                <div class="field">
                  <p class="control is-expanded has-icons-left">
                    <input class="input is-shadowless" type="text" placeholder="http://url-to-stream" v-model="url" :disabled="loading" ref="url_field">
                    <span class="icon is-left">
                      <i class="mdi mdi-web"></i>
                    </span>
                  </p>
                </div>
              </form>
            </div>
            <footer class="card-footer" v-if="loading">
              <a class="card-footer-item has-text-dark">
                <span class="icon"><i class="mdi mdi-web"></i></span> <span class="is-size-7">Loading ...</span>
              </a>
            </footer>
            <footer class="card-footer" v-else>
              <a class="card-footer-item has-text-danger" @click="$emit('close')">
                <span class="icon"><i class="mdi mdi-cancel"></i></span> <span class="is-size-7">Cancel</span>
              </a>
              <a class="card-footer-item has-text-dark" @click="add_stream">
                <span class="icon"><i class="mdi mdi-playlist-plus"></i></span> <span class="is-size-7">Add</span>
              </a>
              <a class="card-footer-item has-background-info has-text-white has-text-weight-bold" @click="play">
                <span class="icon"><i class="mdi mdi-play"></i></span> <span class="is-size-7">Play</span>
              </a>
            </footer>
          </div>
        </div>
        <button class="modal-close is-large" aria-label="close" @click="$emit('close')"></button>
      </div>
    </transition>
  </div>
</template>

<script>
import webapi from '@/webapi'

export default {
  name: 'ModalDialogAddUrlStream',
  props: ['show'],

  data () {
    return {
      url: '',
      loading: false
    }
  },

  methods: {
    add_stream: function () {
      this.loading = true
      webapi.queue_add(this.url).then(() => {
        this.$emit('close')
        this.url = ''
      }).catch(() => {
        this.loading = false
      })
    },

    play: function () {
      this.loading = true
      webapi.player_play_uri(this.url, false).then(() => {
        this.$emit('close')
        this.url = ''
      }).catch(() => {
        this.loading = false
      })
    }
  },

  watch: {
    'show' () {
      if (this.show) {
        this.loading = false

        // We need to delay setting the focus to the input field until the field is part of the dom and visible
        setTimeout(() => {
          this.$refs.url_field.focus()
        }, 10)
      }
    }
  }
}
</script>

<style>
</style>
