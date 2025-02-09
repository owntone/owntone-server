<template>
  <modal-dialog :actions="actions" :show="show" @close="$emit('close')">
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
  </modal-dialog>
</template>

<script>
import ModalDialog from '@/components/ModalDialog.vue'
import webapi from '@/webapi'

export default {
  name: 'ModalDialogAddRss',
  components: { ModalDialog },
  props: { show: Boolean },
  emits: ['close', 'podcast-added'],
  data() {
    return {
      disabled: true,
      loading: false,
      url: ''
    }
  },
  computed: {
    actions() {
      if (this.loading) {
        return [{ label: this.$t('dialog.add.rss.processing'), icon: 'web' }]
      }
      return [
        {
          label: this.$t('dialog.add.rss.cancel'),
          handler: this.cancel,
          icon: 'cancel'
        },
        {
          label: this.$t('dialog.add.rss.add'),
          disabled: this.disabled,
          handler: this.add,
          icon: 'playlist-plus'
        }
      ]
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
    add() {
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
    cancel() {
      this.$emit('close')
    },
    check_url(event) {
      const { validity } = event.target
      this.disabled = validity.patternMismatch || validity.valueMissing
    }
  }
}
</script>
