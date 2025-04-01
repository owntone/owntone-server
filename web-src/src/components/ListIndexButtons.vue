<template>
  <section>
    <nav class="buttons is-centered mb-4">
      <router-link
        v-for="index in indices"
        :key="index"
        class="button is-small p-2 is-index"
        :to="{ hash: `#index_${index}`, query: $route.query }"
      >
        {{ index }}
      </router-link>
    </nav>
  </section>
</template>

<script>
export default {
  name: 'ListIndexButtons',
  props: { indices: { required: true, type: Array } }
}
</script>

<style scoped>
.is-index {
  height: 1.75rem;
  min-width: 1.75rem;
}
</style>
