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
          <li v-for="directory in directories" :key="directory.path">
            <a @click="open(directory)" v-text="directory.name" />
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

<script setup>
import { computed, ref } from 'vue'
import { useRoute, useRouter } from 'vue-router'
import ListItem from '@/components/ListItem.vue'
import ModalDialogDirectory from '@/components/ModalDialogDirectory.vue'
import { useConfigurationStore } from '@/stores/configuration'

defineProps({ items: { required: true, type: Array } })

const configurationStore = useConfigurationStore()
const route = useRoute()
const router = useRouter()

const selectedItem = ref('')
const showDetailsModal = ref(false)

const basename = (path) => path.slice(path.lastIndexOf('/') + 1)

const current = computed(() => route.query?.directory)

/*
 * The library directory the current directory belongs to. Only the library
 * directories and their descendants are guaranteed to exist in the database,
 * so the breadcrumb must not offer to navigate above them.
 */
const root = computed(() => {
  const path = current.value
  if (!path) {
    return null
  }
  return (
    configurationStore.directories
      .filter(
        (directory) => path === directory || path.startsWith(`${directory}/`)
      )
      .sort((a, b) => b.length - a.length)
      .at(0) ?? null
  )
})

const directories = computed(() => {
  const path = current.value
  if (!path || !root.value || path === root.value) {
    return []
  }
  const items = [{ name: basename(root.value), path: root.value }]
  let parent = root.value
  path
    .slice(root.value.length + 1)
    .split('/')
    .slice(0, -1)
    .forEach((name) => {
      parent = `${parent}/${name}`
      items.push({ name, path: parent })
    })
  return items
})

const open = (item) => {
  router.push({ name: 'files', query: { directory: item.path } })
}

const openDetails = (item) => {
  selectedItem.value = item.path
  showDetailsModal.value = true
}

const openParent = () => {
  const parent = directories.value.at(-1)
  if (parent) {
    open(parent)
  } else {
    router.push({ name: 'files' })
  }
}
</script>
