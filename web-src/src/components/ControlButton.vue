<template>
  <button
    class="button is-small is-rounded"
    :disabled="disabled"
    @click="handler"
  >
    <mdicon v-if="icon" class="icon" :name="icon" size="16" />
    <span v-if="label" v-text="$t(label)" />
  </button>
</template>

<script>
export default {
  name: 'ControlButton',
  props: {
    disabled: { default: false, type: Boolean },
    handler: { required: true, type: Function },
    icon: { default: '', type: String },
    label: { default: '', type: String }
  }
}
</script>
