<template>
  <section>
    <nav class="buttons is-centered fd-is-square" style="margin-bottom: 16px">
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
    nav: function (id) {
      this.$router.push({ hash: '#index_' + id })
    }
  }
}
</script>

<style></style>
