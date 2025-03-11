<template>
  <modal-dialog
    :actions="actions"
    :show="show"
    :title="$t('dialog.update.title')"
    @close="$emit('close')"
  >
    <template #content>
      <div v-if="!libraryStore.updating">
        <div
          v-if="servicesStore.isSpotifyEnabled || rss.tracks > 0"
          class="field"
        >
          <label class="label" v-text="$t('dialog.update.info')" />
          <div class="control">
            <div class="select is-small">
              <select v-model="libraryStore.update_dialog_scan_kind">
                <option value="" v-text="$t('dialog.update.all')" />
                <option value="files" v-text="$t('dialog.update.local')" />
                <option
                  v-if="servicesStore.isSpotifyEnabled"
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
        <control-switch v-model="rescan_metadata">
          <template #label>
            <span v-text="$t('dialog.update.rescan-metadata')" />
          </template>
        </control-switch>
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
    actions() {
      const actions = [
        { handler: this.cancel, icon: 'cancel', key: 'actions.cancel' }
      ]
      if (!this.libraryStore.updating) {
        actions.push({
          handler: this.analyse,
          icon: 'check',
          key: 'actions.rescan'
        })
      }
      return actions
    },
    rss() {
      return this.libraryStore.rss
    }
  },
  methods: {
    analyse() {
      if (this.rescan_metadata) {
        webapi.library_rescan(this.libraryStore.update_dialog_scan_kind)
      } else {
        webapi.library_update(this.libraryStore.update_dialog_scan_kind)
      }
    },
    cancel() {
      this.$emit('close')
      this.libraryStore.update_dialog_scan_kind = ''
    }
  }
}
</script>
