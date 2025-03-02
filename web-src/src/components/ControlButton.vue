<template>
  <button
    class="button is-small is-rounded"
    :disabled="button.disabled"
    @click="button.handler"
  >
    <mdicon v-if="button.icon" class="icon" :name="button.icon" size="16" />
    <span v-if="button.key" v-text="$t(button.key)" />
  </button>
</template>

<script>
export default {
  name: 'ControlButton',
  props: {
    button: { required: true, type: Object }
  }
}
</script>
