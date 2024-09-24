<template>
  <base-modal :show="show" @close="$emit('close')">
    <template #content>
      <p class="title is-4" v-text="$t('dialog.add.rss.title')" />
      <div class="field">
        <p class="control has-icons-left">
          <input
            ref="url_field"
            v-model="url"
            class="input"
            type="url"
            pattern="http[s]?://.+"
            required
            :placeholder="$t('dialog.add.rss.placeholder')"
            :disabled="loading"
            @input="check_url"
          />
          <mdicon class="icon is-left" name="rss" size="16" />
        </p>
        <p class="help" v-text="$t('dialog.add.rss.help')" />
      </div>
    </template>
    <template v-if="loading" #footer>
      <a class="card-footer-item has-text-dark">
        <mdicon class="icon" name="web" size="16" />
        <span class="is-size-7" v-text="$t('dialog.add.rss.processing')" />
      </a>
    </template>
    <template v-else #footer>
      <a class="card-footer-item has-text-dark" @click="$emit('close')">
        <mdicon class="icon" name="cancel" size="16" />
        <span class="is-size-7" v-text="$t('dialog.add.rss.cancel')" />
      </a>
      <a
        :class="{ 'is-disabled': disabled }"
        class="card-footer-item"
        @click="add_stream"
      >
        <mdicon class="icon" name="playlist-plus" size="16" />
        <span class="is-size-7" v-text="$t('dialog.add.rss.add')" />
      </a>
    </template>
  </base-modal>
</template>

<script>
import BaseModal from '@/components/BaseModal.vue'
import webapi from '@/webapi'

export default {
  name: 'ModalDialogAddRss',
  components: { BaseModal },
  props: { show: Boolean },
  emits: ['close', 'podcast-added'],

  data() {
    return {
      disabled: true,
      loading: false,
      url: ''
    }
  },

  watch: {
    show() {
      if (this.show) {
        this.loading = false
        // Delay setting the focus on the input field until it is part of the DOM and visible
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
    },
    check_url(event) {
      const { validity } = event.target
      this.disabled = validity.patternMismatch || validity.valueMissing
    }
  }
}
</script>
