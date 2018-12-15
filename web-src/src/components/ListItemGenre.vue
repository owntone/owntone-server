<template>
  <div class="media" :id="'index_' + anchor">
    <div class="media-content fd-has-action is-clipped" v-on:click="open_genre">
      <h1 class="title is-6">{{ genre.name }}</h1>
    </div>
    <div class="media-right">
      <slot name="actions"></slot>
    </div>
  </div>
</template>

<script>
export default {
  name: 'ListItemGenre',
  components: {},

  props: [ 'genre', 'anchor' ],

  methods: {
    open_genre: function () {
      this.show_details_modal = false
      this.$router.push({ name: 'Genre', params: { genre: this.genre.name } })
    }
  }
}
</script>

<style>
</style>
