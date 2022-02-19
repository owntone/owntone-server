<template>
  <div class="media">
    <figure v-if="$slots.icon" class="media-left fd-has-action">
      <slot name="icon" />
    </figure>
    <div class="media-content fd-has-action is-clipped">
      <h1 class="title is-6">
        {{ playlist.name }}
      </h1>
    </div>
    <div class="media-right">
      <slot name="actions" />
    </div>
  </div>
</template>

<script>
export default {
  name: 'ListItemPlaylist',
  props: ['playlist']
}
</script>

<style></style>
