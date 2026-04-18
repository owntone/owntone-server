<template>
  <div class="field">
    <div class="control has-icons-left">
      <input
        ref="input"
        v-model="value"
        class="input"
        type="url"
        pattern="http[s]?://.+"
        required
        :placeholder="placeholder"
        :disabled="loading"
        @input="validate"
      />
      <mdicon class="icon is-left" :name="icon" size="16" />
    </div>
    <div v-if="help" class="help" v-text="help" />
  </div>
</template>

<script setup>
import { onMounted, ref } from 'vue'

defineProps({
  help: { default: '', type: String },
  icon: { required: true, type: String },
  loading: { default: false, type: Boolean },
  placeholder: { required: true, type: String }
})

const emit = defineEmits(['input'])

const value = ref('')
const input = ref(null)

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
