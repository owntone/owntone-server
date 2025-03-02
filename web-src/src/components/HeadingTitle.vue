<template>
  <div v-if="content.title" class="title is-4" v-text="content.title" />
  <div class="is-size-7 is-uppercase">
    <template v-for="(part, index) in content.subtitle" :key="index">
      <a
        v-if="part.handler"
        @click="part.handler"
        v-text="$t(part.key, { count: $n(part.count) }, part.count)"
      />
      <span
        v-else
        v-text="$t(part.key, { count: $n(part.count) }, part.count)"
      />
      <span v-if="index !== content.subtitle.length - 1">&nbsp;|&nbsp;</span>
    </template>
  </div>
</template>

<script>
export default {
  name: 'HeadingTitle',
  props: { content: { required: true, type: Object } }
}
</script>
