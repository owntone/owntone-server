<template>
  <div
    v-click-away="deactivate"
    class="dropdown"
    :class="{ 'is-active': active }"
  >
    <div class="dropdown-trigger">
      <button
        class="button"
        aria-haspopup="true"
        aria-controls="dropdown"
        @click="active = !active"
      >
        <span v-text="option.name" />
        <mdicon class="icon" name="chevron-down" size="16" />
      </button>
    </div>
    <div id="dropdown" class="dropdown-menu" role="menu">
      <div class="dropdown-content">
        <a
          v-for="o in options"
          :key="o.id"
          class="dropdown-item"
          :class="{ 'is-active': value === o.id }"
          @click="select(o)"
          v-text="o.name"
        />
      </div>
    </div>
  </div>
</template>

<script setup>
import { computed, ref } from 'vue'

const props = defineProps({
  options: { required: true, type: Array },
  value: { required: true, type: [String, Number] }
})
const emit = defineEmits(['update:value'])
const active = ref(false)

const option = computed(() =>
  props.options.find((item) => item.id === props.value)
)

const deactivate = () => {
  active.value = false
}

const select = (item) => {
  active.value = false
  emit('update:value', item.id)
}
</script>
