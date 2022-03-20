<template>
  <div>
    <transition name="fade">
      <div v-if="show" class="modal is-active">
        <div class="modal-background" @click="$emit('close')" />
        <div class="modal-content fd-modal-card">
          <div class="card">
            <div class="card-content">
              <p class="title is-4">Add Podcast RSS feed URL</p>
              <form @submit.prevent="add_stream">
                <div class="field">
                  <p class="control is-expanded has-icons-left">
                    <input
                      ref="url_field"
                      v-model="url"
                      class="input is-shadowless"
                      type="text"
                      placeholder="http://url-to-rss"
                      :disabled="loading"
                    />
                    <span class="icon is-left">
                      <i class="mdi mdi-rss" />
                    </span>
                  </p>
                  <p class="help">
                    Adding a podcast includes creating an RSS playlist, that
                    will allow OwnTone to manage the podcast subscription.
                  </p>
                </div>
              </form>
            </div>
            <footer v-if="loading" class="card-footer">
              <a class="card-footer-item button is-loading">
                <span class="icon"><i class="mdi mdi-web" /></span>
                <span class="is-size-7">Processing ...</span>
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
              <a
                class="card-footer-item has-background-info has-text-white has-text-weight-bold"
                @click="add_stream"
              >
                <span class="icon"><i class="mdi mdi-playlist-plus" /></span>
                <span class="is-size-7">Add</span>
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
  name: 'ModalDialogAddRss',
  props: ['show'],
  emits: ['close', 'podcast-added'],

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
        .library_add(this.url)
        .then(() => {
          this.$emit('close')
          this.$emit('podcast-added')
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
