<template>
  <div
    v-if="$route.query.directory"
    class="media is-align-items-center"
    @click="open_parent()"
  >
    <figure class="media-left is-clickable">
      <mdicon class="icon" name="subdirectory-arrow-left" size="16" />
    </figure>
    <div class="media-content is-clickable is-clipped">
      <h1 class="title is-6">..</h1>
    </div>
    <div class="media-right">
      <slot name="actions" />
    </div>
  </div>
  <template v-for="item in items" :key="item.path">
    <div class="media is-align-items-center" @click="open(item)">
      <figure class="media-left is-clickable">
        <mdicon class="icon" name="folder" size="16" />
      </figure>
      <div class="media-content is-clickable is-clipped">
        <h1
          class="title is-6"
          v-text="item.path.substring(item.path.lastIndexOf('/') + 1)"
        />
        <h2 class="subtitle is-7 has-text-grey" v-text="item.path" />
      </div>
      <div class="media-right">
        <a @click.prevent.stop="open_dialog(item)">
          <mdicon class="icon has-text-dark" name="dots-vertical" size="16" />
        </a>
      </div>
    </div>
  </template>
  <teleport to="#app">
    <modal-dialog-directory
      :item="selected_item"
      :show="show_details_modal"
      @close="show_details_modal = false"
    />
  </teleport>
</template>

<script>
import ModalDialogDirectory from '@/components/ModalDialogDirectory.vue'

export default {
  name: 'ListDirectories',
  components: { ModalDialogDirectory },
  props: { items: { required: true, type: Array } },

  data() {
    return {
      selected_item: '',
      show_details_modal: false
    }
  },

  computed: {
    current() {
      if (this.$route.query && this.$route.query.directory) {
        return this.$route.query.directory
      }
      return '/'
    }
  },

  methods: {
    open(item) {
      this.$router.push({
        name: 'files',
        query: { directory: item.path }
      })
    },
    open_dialog(item) {
      this.selected_item = item.path
      this.show_details_modal = true
    },
    open_parent() {
      const parent = this.current.slice(0, this.current.lastIndexOf('/'))
      if (
        parent === '' ||
        this.$store.state.config.directories.includes(this.current)
      ) {
        this.$router.push({ name: 'files' })
      } else {
        this.$router.push({
          name: 'files',
          query: {
            directory: this.current.slice(0, this.current.lastIndexOf('/'))
          }
        })
      }
    }
  }
}
</script>

<style></style>
