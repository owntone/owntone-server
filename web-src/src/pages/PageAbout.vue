<template>
  <content-with-heading>
    <template #heading>
      <pane-title :content="{ title: $t('page.about.library') }" />
    </template>
    <template #actions>
      <control-button
        :button="{
          handler: openUpdateDialog,
          icon: 'refresh',
          key: 'page.about.update'
        }"
        :class="{ 'is-loading': libraryStore.updating }"
        :disabled="libraryStore.updating"
      />
    </template>
    <template #content>
      <div
        v-for="property in properties"
        :key="property.key"
        class="media is-align-items-center mb-0"
      >
        <div
          class="media-content has-text-weight-bold"
          v-text="$t(property.key)"
        />
        <div class="media-right">
          <span v-text="property.value" />
          <span
            v-if="property.alternate"
            class="has-text-grey"
            v-text="` (${property.alternate})`"
          />
        </div>
      </div>
      <div
        class="is-size-7 mt-6"
        v-text="
          $t('page.about.version', {
            version: configurationStore.version
          })
        "
      />
      <div
        class="is-size-7"
        v-text="
          $t('page.about.compiled-with', {
            options: configurationStore.buildoptions.join(', ')
          })
        "
      />
      <i18n-t
        tag="div"
        class="is-size-7"
        keypath="page.about.built-with"
        scope="global"
      >
        <template #bulma>
          <a href="https://bulma.io">Bulma</a>
        </template>
        <template #mdi>
          <a href="https://pictogrammers.com/library/mdi/">
            Material Design Icons
          </a>
        </template>
        <template #vuejs>
          <a href="https://vuejs.org/">Vue.js</a>
        </template>
        <template #axios>
          <a href="https://github.com/axios/axios">axios</a>
        </template>
        <template #others>
          <a
            href="https://github.com/owntone/owntone-server/network/dependencies"
            v-text="$t('page.about.more')"
          />
        </template>
      </i18n-t>
    </template>
  </content-with-heading>
</template>

<script setup>
import ContentWithHeading from '@/templates/ContentWithHeading.vue'
import ControlButton from '@/components/ControlButton.vue'
import PaneTitle from '@/components/PaneTitle.vue'
import { computed } from 'vue'
import formatters from '@/lib/Formatters'
import { useConfigurationStore } from '@/stores/configuration'
import { useI18n } from 'vue-i18n'
import { useLibraryStore } from '@/stores/library'
import { useUIStore } from '@/stores/ui'

const configurationStore = useConfigurationStore()
const libraryStore = useLibraryStore()
const { n } = useI18n()
const uiStore = useUIStore()

const properties = computed(() => [
  { key: 'property.name', value: configurationStore.library_name },
  { key: 'property.artists', value: n(libraryStore.artists) },
  { key: 'property.albums', value: n(libraryStore.albums) },
  { key: 'property.tracks', value: n(libraryStore.songs) },
  {
    key: 'property.playtime',
    value: formatters.toDuration(libraryStore.db_playtime)
  },
  {
    alternate: formatters.toDateTime(libraryStore.updated_at),
    key: 'property.updated',
    value: formatters.toRelativeDuration(libraryStore.updated_at)
  },
  {
    alternate: formatters.toDateTime(libraryStore.started_at),
    key: 'property.uptime',
    value: formatters.toDurationToNow(libraryStore.started_at)
  }
])

const openUpdateDialog = () => {
  uiStore.showUpdateDialog = true
}
</script>
