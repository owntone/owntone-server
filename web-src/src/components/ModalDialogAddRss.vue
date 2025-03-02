<template>
  <modal-dialog
    :actions="actions"
    :show="show"
    :title="$t('dialog.add.rss.title')"
    @close="$emit('close')"
  >
    <template #content>
      <control-url-field
        icon="rss"
        :help="$t('dialog.add.rss.help')"
        :loading="loading"
        :placeholder="$t('dialog.add.rss.placeholder')"
        @url-changed="onUrlChanged"
      />
    </template>
  </modal-dialog>
</template>

<script>
import ControlUrlField from '@/components/ControlUrlField.vue'
import ModalDialog from '@/components/ModalDialog.vue'
import webapi from '@/webapi'

export default {
  name: 'ModalDialogAddRss',
  components: { ControlUrlField, ModalDialog },
  props: { show: Boolean },
  emits: ['close', 'podcast-added'],
  data() {
    return {
      loading: false,
      disabled: true,
      url: ''
    }
  },
  computed: {
    actions() {
      if (this.loading) {
        return [{ key: 'dialog.add.rss.processing', icon: 'web' }]
      }
      return [
        { key: 'dialog.add.rss.cancel', handler: this.cancel, icon: 'cancel' },
        {
          key: 'dialog.add.rss.add',
          disabled: this.disabled,
          handler: this.add,
          icon: 'playlist-plus'
        }
      ]
    }
  },
  methods: {
    onUrlChanged(url, disabled) {
      this.url = url
      this.disabled = disabled
    },
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
    }
  }
}
</script>
