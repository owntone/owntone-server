<template>
  <modal-dialog
    :actions="actions"
    :show="show"
    :title="$t('dialog.add.stream.title')"
    @close="$emit('close')"
  >
    <template #content>
      <form @submit.prevent="play">
        <control-url-field
          icon="web"
          :loading="loading"
          :placeholder="$t('dialog.add.stream.placeholder')"
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
import queue from '@/api/queue'

defineProps({ show: Boolean })

const emit = defineEmits(['close'])

const disabled = ref(true)
const loading = ref(false)
const url = ref('')

const cancel = () => {
  emit('close')
}

const add = async () => {
  loading.value = true
  const urlValue = url.value
  url.value = ''
  try {
    await queue.addUri(urlValue)
    emit('close')
  } finally {
    loading.value = false
  }
}

const play = async () => {
  loading.value = true
  const urlValue = url.value
  url.value = ''
  try {
    await queue.playUri(urlValue, false)
    emit('close')
  } finally {
    loading.value = false
  }
}

const onUrlChange = (newUrl, isDisabled) => {
  url.value = newUrl
  disabled.value = isDisabled
}

const actions = computed(() => {
  if (loading.value) {
    return [{ icon: 'web', key: 'dialog.add.stream.processing' }]
  }
  return [
    { handler: cancel, icon: 'cancel', key: 'actions.cancel' },
    {
      disabled: disabled.value,
      handler: add,
      icon: 'playlist-plus',
      key: 'actions.add'
    },
    {
      disabled: disabled.value,
      handler: play,
      icon: 'play',
      key: 'actions.play'
    }
  ]
})
</script>
