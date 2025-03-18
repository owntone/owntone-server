<template>
  <div v-if="$route.query.directory" class="media is-align-items-center mb-0">
    <mdicon
      class="icon media-left is-clickable"
      name="chevron-left"
      @click="openParent"
    />
    <div class="media-content">
      <nav class="breadcrumb">
        <ul>
          <li v-for="directory in directories" :key="directory.index">
            <a @click="open(directory)">
              <span v-text="directory.name" />
            </a>
          </li>
        </ul>
      </nav>
    </div>
    <div class="media-right">
      <slot name="actions" />
    </div>
  </div>
  <list-item
    v-for="item in items"
    :key="item.path"
    icon="folder"
    :lines="[item.name]"
    @open="open(item)"
    @open-details="openDetails(item)"
  />
  <modal-dialog-directory
    :item="selectedItem"
    :show="showDetailsModal"
    @close="showDetailsModal = false"
  />
</template>

<script>
import ListItem from '@/components/ListItem.vue'
import ModalDialogDirectory from '@/components/ModalDialogDirectory.vue'

export default {
  name: 'ListDirectories',
  components: { ListItem, ModalDialogDirectory },
  props: { items: { required: true, type: Array } },
  data() {
    return { selectedItem: '', showDetailsModal: false }
  },
  computed: {
    directories() {
      const directories = []
      let path = ''
      this.$route.query?.directory
        .split('/')
        .slice(1, -1)
        .forEach((name, index) => {
          path = `${path}/${name}`
          directories.push({ index, name, path })
        })
      return directories
    }
  },
  methods: {
    open(item) {
      const route = { name: 'files' }
      if (item.index !== 0) {
        route.query = { directory: item.path }
      }
      this.$router.push(route)
    },
    openDetails(item) {
      this.selectedItem = item.path
      this.showDetailsModal = true
    },
    openParent() {
      this.open(this.directories.slice(-1).pop())
    }
  }
}
</script>
