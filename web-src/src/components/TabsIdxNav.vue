<template>
  <a href="#" v-scroll-to="{ el: '#' + link.a, offset: -50 }">{{ link.n }}</a>
</template>

<script>
export default {
  name: 'TabsIdxNav',

  props: ['link']
}
</script>

<style>
</style>
