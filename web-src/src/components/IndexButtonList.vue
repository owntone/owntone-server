<template>
  <section>
    <nav class="buttons is-centered mb-4">
      <router-link
        v-for="index in indices"
        :key="index"
        class="button is-small is-square"
        :to="{ hash: `#index_${index}`, query: $route.query }"
      >
        {{ index }}
      </router-link>
    </nav>
  </section>
</template>

<script>
export default {
  name: 'IndexButtonList',
  props: { indices: { required: true, type: Array } }
}
</script>

<style scoped>
.is-square {
  height: 1.75rem;
  min-width: 1.75rem;
  padding-left: 0.25rem;
  padding-right: 0.25rem;
}
</style>
