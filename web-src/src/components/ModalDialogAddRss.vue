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
        @input="onUrlChange"
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
      disabled: true,
      loading: false,
      url: ''
    }
  },
  computed: {
    actions() {
      if (this.loading) {
        return [{ icon: 'web', key: 'dialog.add.rss.processing' }]
      }
      return [
        { handler: this.cancel, icon: 'cancel', key: 'actions.cancel' },
        {
          disabled: this.disabled,
          handler: this.add,
          icon: 'playlist-plus',
          key: 'actions.add'
        }
      ]
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
    onUrlChange(url, disabled) {
      this.url = url
      this.disabled = disabled
    }
  }
}
</script>
