<template>
  <modal-dialog
    :actions="actions"
    :show="show"
    :title="$t('dialog.update.title')"
    @close="$emit('close')"
  >
    <template #content>
      <div v-if="!libraryStore.updating">
        <div v-if="servicesStore.isSpotifyActive" class="field">
          <label class="label" v-text="$t('dialog.update.info')" />
          <div class="control">
            <div class="select is-small">
              <select v-model="libraryStore.update_dialog_scan_kind">
                <option value="" v-text="$t('dialog.update.all')" />
                <option value="files" v-text="$t('dialog.update.local')" />
                <option
                  v-if="servicesStore.isSpotifyActive"
                  value="spotify"
                  v-text="$t('dialog.update.spotify')"
                />
                <option value="rss" v-text="$t('dialog.update.feeds')" />
              </select>
            </div>
          </div>
        </div>
        <control-switch v-model="rescanMetadata">
          <template #label>
            <span v-text="$t('dialog.update.rescan-metadata')" />
          </template>
        </control-switch>
      </div>
      <div v-else>
        <div class="mb-3" v-text="$t('dialog.update.progress')" />
      </div>
    </template>
  </modal-dialog>
</template>

<script setup>
import { computed, ref } from 'vue'
import ControlSwitch from '@/components/ControlSwitch.vue'
import ModalDialog from '@/components/ModalDialog.vue'
import library from '@/api/library'
import { useLibraryStore } from '@/stores/library'
import { useServicesStore } from '@/stores/services'

defineProps({ show: Boolean })

const emit = defineEmits(['close'])

const libraryStore = useLibraryStore()
const servicesStore = useServicesStore()

const rescanMetadata = ref(false)

const analyse = () => {
  if (rescanMetadata.value) {
    library.rescan(libraryStore.update_dialog_scan_kind)
  } else {
    library.update(libraryStore.update_dialog_scan_kind)
  }
}

const cancel = () => {
  libraryStore.update_dialog_scan_kind = ''
  emit('close')
}

const actions = computed(() => {
  const items = [{ handler: cancel, icon: 'cancel', key: 'actions.cancel' }]
  if (!libraryStore.updating) {
    items.push({ handler: analyse, icon: 'check', key: 'actions.rescan' })
  }
  return items
})
</script>
