<template>
  <modal-dialog
    :show="show"
    :title="$t('dialog.update.title')"
    :ok_action="libraryStore.updating ? '' : $t('dialog.update.rescan')"
    :close_action="$t('dialog.update.cancel')"
    @ok="update_library"
    @close="close()"
  >
    <template #modal-content>
      <div v-if="!libraryStore.updating">
        <p class="mb-3" v-text="$t('dialog.update.info')" />
        <div v-if="spotify_enabled || rss.tracks > 0" class="field">
          <div class="control">
            <div class="select is-small">
              <select v-model="libraryStore.update_dialog_scan_kind">
                <option value="" v-text="$t('dialog.update.all')" />
                <option value="files" v-text="$t('dialog.update.local')" />
                <option
                  v-if="spotify_enabled"
                  value="spotify"
                  v-text="$t('dialog.update.spotify')"
                />
                <option
                  v-if="rss.tracks > 0"
                  value="rss"
                  v-text="$t('dialog.update.feeds')"
                />
              </select>
            </div>
          </div>
        </div>
        <div class="field">
          <control-switch v-model="rescan_metadata">
            <template #label>
              <span v-text="$t('dialog.update.rescan-metadata')" />
            </template>
          </control-switch>
        </div>
      </div>
      <div v-else>
        <p class="mb-3" v-text="$t('dialog.update.progress')" />
      </div>
    </template>
  </modal-dialog>
</template>

<script>
import ControlSwitch from '@/components/ControlSwitch.vue'
import ModalDialog from '@/components/ModalDialog.vue'
import { useLibraryStore } from '@/stores/library'
import { useServicesStore } from '@/stores/services'
import webapi from '@/webapi'

export default {
  name: 'ModalDialogUpdate',
  components: { ControlSwitch, ModalDialog },
  props: { show: Boolean },
  emits: ['close'],

  setup() {
    return {
      libraryStore: useLibraryStore(),
      servicesStore: useServicesStore()
    }
  },

  data() {
    return {
      rescan_metadata: false
    }
  },

  computed: {
    rss() {
      return this.libraryStore.rss
    },
    spotify_enabled() {
      return this.servicesStore.spotify.webapi_token_valid
    }
  },

  methods: {
    close() {
      this.libraryStore.update_dialog_scan_kind = ''
      this.$emit('close')
    },
    update_library() {
      if (this.rescan_metadata) {
        webapi.library_rescan(this.libraryStore.update_dialog_scan_kind)
      } else {
        webapi.library_update(this.libraryStore.update_dialog_scan_kind)
      }
    }
  }
}
</script>
