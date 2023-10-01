<template>
  <section>
    <nav class="buttons is-centered mb-4 fd-is-square">
      <router-link
        v-for="id in index"
        :key="id"
        class="button is-small"
        :to="'#index_' + id"
        >{{ id }}</router-link
      >
    </nav>
  </section>
</template>

<script>
export default {
  name: 'IndexButtonList',
  props: ['index']
}
</script>

<style></style>
