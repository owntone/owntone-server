<template>
  <template v-for="genre in items" :key="genre.itemId">
    <div v-if="!genre.isItem" class="mt-6 mb-5 py-2">
      <div class="media-content is-clipped">
        <span
          :id="'index_' + genre.index"
          class="tag is-info is-light is-small has-text-weight-bold"
          v-text="genre.index"
        />
      </div>
    </div>
    <div
      v-else
      class="media is-align-items-center"
      @click="open_genre(genre.item)"
    >
      <div class="media-content is-clickable is-clipped">
        <h1 class="title is-6" v-text="genre.item.name" />
      </div>
      <div class="media-right">
        <a @click.prevent.stop="open_dialog(genre.item)">
          <mdicon class="icon has-text-dark" name="dots-vertical" size="16" />
        </a>
      </div>
    </div>
  </template>
  <teleport to="#app">
    <modal-dialog-genre
      :show="show_details_modal"
      :genre="selected_genre"
      :media_kind="media_kind"
      @close="show_details_modal = false"
    />
  </teleport>
</template>

<script>
import ModalDialogGenre from '@/components/ModalDialogGenre.vue'

export default {
  name: 'ListGenres',
  components: { ModalDialogGenre },
  props: {
    items: { required: true, type: Object },
    media_kind: { required: true, type: String }
  },

  data() {
    return {
      selected_genre: {},
      show_details_modal: false
    }
  },

  methods: {
    open_dialog(genre) {
      this.selected_genre = genre
      this.show_details_modal = true
    },
    open_genre(genre) {
      this.$router.push({
        name: 'genre-albums',
        params: { name: genre.name },
        query: { media_kind: this.media_kind }
      })
    }
  }
}
</script>

<style></style>
