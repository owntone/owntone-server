<template>
  <section>
    <nav class="buttons is-centered fd-is-square" style="margin-bottom: 16px;">
      <a v-for="char in filtered_index" :key="char" class="button is-small" @click="nav(char)">{{ char }}</a>
    </nav>
  </section>
</template>

<script>
export default {
  name: 'IndexButtonList',

  props: ['index'],

  computed: {
    filtered_index () {
      const specialChars = '!"#$%&\'()*+,-./:;<=>?@[\\]^`{|}~'
      return this.index.filter(c => !specialChars.includes(c))
    }
  },

  methods: {
    nav: function (id) {
      this.$router.push({ hash: '#index_' + id })
    },

    scroll_to_top: function () {
      window.scrollTo({ top: 0, behavior: 'smooth' })
    }
  }
}
</script>

<style>
</style>
