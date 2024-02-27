<template>
  <div>
    <transition name="fade">
      <div v-if="show" class="modal is-active">
        <div class="modal-background" @click="$emit('close')" />
        <div class="modal-content fd-modal-card">
          <form class="card" @submit.prevent="add_stream">
            <div class="card-content">
              <p class="title is-4" v-text="$t('dialog.add.rss.title')" />
              <div class="field">
                <p class="control has-icons-left">
                  <input
                    ref="url_field"
                    v-model="url"
                    class="input is-shadowless"
                    type="url"
                    pattern="http[s]?://.*"
                    required
                    :placeholder="$t('dialog.add.rss.placeholder')"
                    :disabled="loading"
                  />
                  <mdicon class="icon is-left" name="rss" size="16" />
                </p>
                <p class="help" v-text="$t('dialog.add.rss.help')" />
              </div>
            </div>
            <footer v-if="loading" class="card-footer">
              <a class="card-footer-item has-text-dark">
                <mdicon class="icon" name="web" size="16" />
                <span
                  class="is-size-7"
                  v-text="$t('dialog.add.rss.processing')"
                />
              </a>
            </footer>
            <footer v-else class="card-footer is-clipped">
              <a class="card-footer-item has-text-dark" @click="$emit('close')">
                <mdicon class="icon" name="cancel" size="16" />
                <span class="is-size-7" v-text="$t('dialog.add.rss.cancel')" />
              </a>
              <a
                class="card-footer-item has-background-info has-text-white has-text-weight-bold"
                @click="add_stream"
              >
                <mdicon class="icon" name="playlist-plus" size="16" />
                <span class="is-size-7" v-text="$t('dialog.add.rss.add')" />
              </a>
            </footer>
          </form>
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
  props: { show: Boolean },
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
    add_stream() {
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
