<template>
  <section>
    <nav class="buttons is-centered mb-4 fd-is-square">
      <a
        v-for="char in index"
        :key="char"
        class="button is-small"
        @click="nav(char)"
        v-text="char"
      />
    </nav>
  </section>
</template>

<script>
export default {
  name: 'IndexButtonList',
  props: ['index'],
  methods: {
    nav(id) {
      this.$router.push({ hash: '#index_' + id })
    }
  }
}
</script>

<style></style>
