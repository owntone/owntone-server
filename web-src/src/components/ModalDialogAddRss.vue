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

<script setup>
import { computed, ref } from 'vue'

import ControlUrlField from '@/components/ControlUrlField.vue'
import ModalDialog from '@/components/ModalDialog.vue'
import library from '@/api/library'

defineProps({ show: Boolean })

const emit = defineEmits(['close', 'podcast-added'])

const disabled = ref(true)
const loading = ref(false)
const url = ref('')

const add = async () => {
  loading.value = true
  const urlValue = url.value
  url.value = ''
  try {
    await library.add(urlValue)
    emit('podcast-added')
    emit('close')
  } finally {
    loading.value = false
  }
}

const cancel = () => {
  emit('close')
}

const actions = computed(() => {
  if (loading.value) {
    return [{ icon: 'web', key: 'dialog.add.rss.processing' }]
  }
  return [
    { handler: cancel, icon: 'cancel', key: 'actions.cancel' },
    {
      disabled: disabled.value,
      handler: add,
      icon: 'playlist-plus',
      key: 'actions.add'
    }
  ]
})

const onUrlChange = (newUrl, isDisabled) => {
  url.value = newUrl
  disabled.value = isDisabled
}
</script>
