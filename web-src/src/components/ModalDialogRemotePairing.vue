<template>
  <modal-dialog
    :actions="actions"
    :show="show"
    :title="$t('dialog.remote-pairing.title')"
    @close="$emit('close')"
  >
    <template #content>
      <form @submit.prevent="pair">
        <label class="label" v-text="remoteStore.remote" />
        <control-pin-field
          :placeholder="$t('dialog.remote-pairing.pairing-code')"
          @input="onPinChange"
        />
      </form>
    </template>
  </modal-dialog>
</template>

<script setup>
import { computed, ref } from 'vue'
import ControlPinField from '@/components/ControlPinField.vue'
import ModalDialog from '@/components/ModalDialog.vue'
import remotes from '@/api/remotes'
import { useRemotesStore } from '@/stores/remotes'

defineProps({ show: Boolean })

const emit = defineEmits(['close'])

const remoteStore = useRemotesStore()

const disabled = ref(true)
const pin = ref('')

const cancel = () => {
  emit('close')
}

const onPinChange = (newPin, isDisabled) => {
  pin.value = newPin
  disabled.value = isDisabled
}

const pair = async () => {
  const pinValue = pin.value
  pin.value = ''
  await remotes.pair(pinValue)
}

const actions = computed(() => [
  { handler: cancel, icon: 'cancel', key: 'actions.cancel' },
  {
    disabled: disabled.value,
    handler: pair,
    icon: 'vector-link',
    key: 'actions.pair'
  }
])
</script>
