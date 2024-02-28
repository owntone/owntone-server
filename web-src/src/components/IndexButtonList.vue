<template>
  <section>
    <nav class="buttons is-centered mb-4 fd-is-square">
      <router-link
        v-for="index in indices"
        :key="index"
        class="button is-small"
        :to="'#index_' + index"
        >{{ index }}</router-link
      >
    </nav>
  </section>
</template>

<script>
export default {
  name: 'IndexButtonList',
  props: { indices: { required: true, type: Array } }
}
</script>

<style></style>
