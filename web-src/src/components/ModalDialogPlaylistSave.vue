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
          <div class="control has-icons-left">
            <input
              ref="playlist_name_field"
              v-model="playlistName"
              class="input"
              type="text"
              pattern=".+"
              required
              :placeholder="$t('dialog.playlist.save.playlist-name')"
              :disabled="loading"
              @input="check"
            />
            <mdicon class="icon is-left" name="playlist-music" size="16" />
          </div>
        </div>
      </form>
    </template>
  </modal-dialog>
</template>

<script>
import ModalDialog from '@/components/ModalDialog.vue'
import queue from '@/api/queue'

export default {
  name: 'ModalDialogPlaylistSave',
  components: { ModalDialog },
  props: { show: Boolean },
  emits: ['close'],
  data() {
    return {
      disabled: true,
      loading: false,
      playlistName: ''
    }
  },
  computed: {
    actions() {
      if (this.loading) {
        return [{ icon: 'web', key: 'dialog.playlist.save.saving' }]
      }
      return [
        { handler: this.cancel, icon: 'cancel', key: 'actions.cancel' },
        {
          disabled: this.disabled,
          handler: this.save,
          icon: 'download',
          key: 'actions.save'
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
          this.$refs.playlist_name_field.focus()
        }, 10)
      }
    }
  },
  methods: {
    cancel() {
      this.$emit('close')
    },
    check(event) {
      const { validity } = event.target
      this.disabled = validity.patternMismatch || validity.valueMissing
    },
    save() {
      this.loading = true
      queue
        .saveToPlaylist(this.playlistName)
        .then(() => {
          this.$emit('close')
          this.playlistName = ''
        })
        .catch(() => {
          this.loading = false
        })
    }
  }
}
</script>
