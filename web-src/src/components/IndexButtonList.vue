<template>
  <section>
    <nav class="buttons is-centered fd-is-square" style="margin-bottom: 48px;" v-if="filtered_index.length > 1">
      <a v-for="char in filtered_index" :key="char" class="button is-small" @click="nav(char)">{{ char }}</a>
    </nav>
    <nav class="buttons is-centered" style="margin-bottom: 6px;" v-if="filtered_index.length > 1">
      <a class="button is-small is-white" @click="scroll_to_top"><span class="icon is-small"><i class="mdi mdi-chevron-up"></i></span></a>
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
      this.$router.push({ path: this.$router.currentRoute.path + '#index_' + id })
    },

    scroll_to_top: function () {
      window.scrollTo({ top: 0, behavior: 'smooth' })
    }
  }
}
</script>

<style>
</style>
