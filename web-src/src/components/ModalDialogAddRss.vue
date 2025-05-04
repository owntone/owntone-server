<template>
  <modal-dialog
    :actions="actions"
    :show="show"
    :title="$t('dialog.add.rss.title')"
    @close="$emit('close')"
  >
    <template #content>
      <form @submit.prevent="add">
        <control-url-field
          icon="rss"
          :help="$t('dialog.add.rss.help')"
          :loading="loading"
          :placeholder="$t('dialog.add.rss.placeholder')"
          @input="onUrlChange"
        />
      </form>
    </template>
  </modal-dialog>
</template>

<script>
import ControlUrlField from '@/components/ControlUrlField.vue'
import ModalDialog from '@/components/ModalDialog.vue'
import library from '@/api/library'

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
      library
        .add(this.url)
        .then(() => {
          this.$emit('podcast-added')
          this.$emit('close')
        })
        .finally(() => {
          this.url = ''
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
