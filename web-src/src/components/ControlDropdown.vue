<template>
  <div class="select">
    <select @change="select($event.target.value)">
      <option
        v-for="option in options"
        :key="option.id"
        :value="option.id"
        :selected="value === option.id"
        v-text="option.name"
      />
    </select>
  </div>
</template>

<script setup>
const props = defineProps({
  options: { required: true, type: Array },
  value: { required: true, type: [String, Number] }
})
const emit = defineEmits(['update:value'])

const select = (id) => {
  const item = props.options.find((option) => String(option.id) === id)
  emit('update:value', item.id)
}
</script>
