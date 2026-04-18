<template>
  <div class="field">
    <div class="control">
      <input
        ref="input"
        v-model="value"
        class="input"
        type="text"
        inputmode="numeric"
        pattern="[\d]{4}"
        :placeholder="placeholder"
        @input="validate"
      />
    </div>
    <slot />
  </div>
</template>

<script setup>
import { onMounted, ref } from 'vue'

defineProps({ placeholder: { required: true, type: String } })

const emit = defineEmits(['input'])

const input = ref(null)
const value = ref('')

onMounted(() => {
  setTimeout(() => {
    input.value?.focus()
  }, 10)
})

const validate = (event) => {
  const { validity } = event.target
  const invalid = validity.patternMismatch || validity.valueMissing
  emit('input', value.value, invalid)
}
</script>
