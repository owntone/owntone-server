<template>
  <modal-dialog
    :actions="actions"
    :show="show"
    :title="$t('dialog.playlist.save.title')"
    @close="$emit('close')"
  >
    <template #content>
      <form @submit.prevent="save">
        <div class="field">
          <p class="control has-icons-left">
            <input
              ref="playlist_name_field"
              v-model="playlist_name"
              class="input"
              type="text"
              pattern=".+"
              required
              :placeholder="$t('dialog.playlist.save.playlist-name')"
              :disabled="loading"
              @input="check_name"
            />
            <mdicon class="icon is-left" name="file-music" size="16" />
          </p>
        </div>
      </form>
    </template>
  </modal-dialog>
</template>

<script>
import ModalDialog from '@/components/ModalDialog.vue'
import webapi from '@/webapi'

export default {
  name: 'ModalDialogPlaylistSave',
  components: { ModalDialog },
  props: { show: Boolean },
  emits: ['close'],
  data() {
    return {
      disabled: true,
      loading: false,
      playlist_name: ''
    }
  },
  actions() {
    if (loading) {
      return [{ label: this.$t('dialog.playlist.save.saving'), icon: 'web' }]
    }
    return [
      {
        label: this.$t('dialog.playlist.save.cancel'),
        handler: this.cancel,
        icon: 'cancel'
      },
      {
        label: this.$t('dialog.playlist.save.save'),
        handler: this.save,
        icon: 'save'
      }
    ]
  },
  watch: {
    show() {
      if (this.show) {
        this.loading = false
        // Delay setting the focus on the input field until it is part of the DOM and visible
        setTimeout(() => {
          this.$refs.playlist_name_field.focus()
        }, 10)
      }
    }
  },
  methods: {
    cancel() {
      this.$emit('close')
    },
    check_name(event) {
      const { validity } = event.target
      this.disabled = validity.patternMismatch || validity.valueMissing
    },
    save() {
      this.loading = true
      webapi
        .queue_save_playlist(this.playlist_name)
        .then(() => {
          this.$emit('close')
          this.playlist_name = ''
        })
        .catch(() => {
          this.loading = false
        })
    }
  }
}
</script>
