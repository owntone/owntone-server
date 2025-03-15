<template>
  <div class="columns">
    <div v-if="$slots.filter" class="column">
      <div class="is-size-7 is-uppercase" v-text="$t('options.filter.title')" />
      <slot name="filter" />
    </div>
    <div v-if="$slots.sort" class="column">
      <div class="is-size-7 is-uppercase" v-text="$t('options.sort.title')" />
      <slot name="sort" />
    </div>
  </div>
</template>

<script>
export default {
  name: 'ListOptions'
}
</script>
