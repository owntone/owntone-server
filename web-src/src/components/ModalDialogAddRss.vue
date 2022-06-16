<template>
  <div>
    <transition name="fade">
      <div v-if="show" class="modal is-active">
        <div class="modal-background" @click="$emit('close')" />
        <div class="modal-content fd-modal-card">
          <div class="card">
            <div class="card-content">
              <p class="title is-4" v-text="$t('dialog.add.rss.title')" />
              <form @submit.prevent="add_stream">
                <div class="field">
                  <p class="control is-expanded has-icons-left">
                    <input
                      ref="url_field"
                      v-model="url"
                      class="input is-shadowless"
                      type="text"
                      :placeholder="$t('dialog.add.rss.placeholder')"
                      :disabled="loading"
                    />
                    <span class="icon is-left"
                      ><mdicon name="rss" size="16"
                    /></span>
                  </p>
                  <p class="help" v-text="$t('dialog.add.rss.help')" />
                </div>
              </form>
            </div>
            <footer v-if="loading" class="card-footer">
              <a class="card-footer-item button is-loading">
                <span class="icon"><mdicon name="web" size="16" /></span>
                <span
                  class="is-size-7"
                  v-text="$t('dialog.add.rss.processing')"
                />
              </a>
            </footer>
            <footer v-else class="card-footer">
              <a
                class="card-footer-item has-text-danger"
                @click="$emit('close')"
              >
                <span class="icon"><mdicon name="cancel" size="16" /></span>
                <span class="is-size-7" v-text="$t('dialog.add.rss.cancel')" />
              </a>
              <a
                class="card-footer-item has-background-info has-text-white has-text-weight-bold"
                @click="add_stream"
              >
                <span class="icon"
                  ><mdicon name="playlist-plus" size="16"
                /></span>
                <span class="is-size-7" v-text="$t('dialog.add.rss.add')" />
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
