<template>
  <vue-eternal-loading v-if="load" :load="load">
    <template #loading>
      <div class="columns is-centered">
        <div class="column has-text-centered">
          <mdicon class="icon mdi-spin" name="loading" />
        </div>
      </div>
    </template>
    <template #no-more>
      <br />
    </template>
    <template #no-results>
      <br />
    </template>
  </vue-eternal-loading>
</template>

<script setup>
import { VueEternalLoading } from '@ts-pro/vue-eternal-loading'

defineProps({ load: { default: null, type: Function } })
</script>
