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
              ref="playlistNameField"
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

<script setup>
import { computed, nextTick, ref, watch } from 'vue'
import ModalDialog from '@/components/ModalDialog.vue'
import queue from '@/api/queue'

const props = defineProps({ show: Boolean })

const emit = defineEmits(['close'])

const disabled = ref(true)
const loading = ref(false)
const playlistName = ref('')
const playlistNameField = ref(null)

const cancel = () => {
  emit('close')
}

const check = (event) => {
  const { validity } = event.target
  disabled.value = validity.patternMismatch || validity.valueMissing
}

const save = async () => {
  loading.value = true
  const name = playlistName.value
  playlistName.value = ''
  try {
    await queue.saveToPlaylist(name)
    emit('close')
  } finally {
    loading.value = false
  }
}

const actions = computed(() => {
  if (loading.value) {
    return [{ icon: 'web', key: 'dialog.playlist.save.saving' }]
  }
  return [
    { handler: cancel, icon: 'cancel', key: 'actions.cancel' },
    {
      disabled: disabled.value,
      handler: save,
      icon: 'download',
      key: 'actions.save'
    }
  ]
})

watch(
  () => props.show,
  async (isShown) => {
    if (isShown) {
      loading.value = false
      await nextTick()
      setTimeout(() => {
        playlistNameField.value?.focus()
      }, 10)
    }
  }
)
</script>
