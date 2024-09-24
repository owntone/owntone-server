<template>
  <base-modal :show="show" @close="$emit('close')">
    <template #content>
      <form @submit.prevent="save">
        <p class="title is-4" v-text="$t('dialog.playlist.save.title')" />
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
    <template v-if="loading" #footer>
      <a class="card-footer-item has-text-dark">
        <mdicon class="icon" name="web" size="16" />
        <span class="is-size-7" v-text="$t('dialog.playlist.save.saving')" />
      </a>
    </template>
    <template v-else #footer>
      <a class="card-footer-item has-text-danger" @click="$emit('close')">
        <mdicon class="icon" name="cancel" size="16" />
        <span class="is-size-7" v-text="$t('dialog.playlist.save.cancel')" />
      </a>
      <a
        :class="{ 'is-disabled': disabled }"
        class="card-footer-item has-text-weight-bold"
        @click="save"
      >
        <mdicon class="icon" name="content-save" size="16" />
        <span class="is-size-7" v-text="$t('dialog.playlist.save.save')" />
      </a>
    </template>
  </base-modal>
</template>

<script>
import BaseModal from '@/components/BaseModal.vue'
import webapi from '@/webapi'

export default {
  name: 'ModalDialogPlaylistSave',
  components: { BaseModal },
  props: { show: Boolean },
  emits: ['close'],

  data() {
    return {
      disabled: true,
      loading: false,
      playlist_name: ''
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
