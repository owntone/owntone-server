<template>
  <div>
    <content-with-heading>
      <template #heading-left>
        <heading-title :content="{ title: $t('page.about.library') }" />
      </template>
      <template #heading-right>
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
      </template>
      <template #footer>
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
  </div>
</template>

<script>
import ContentWithHeading from '@/templates/ContentWithHeading.vue'
import ControlButton from '@/components/ControlButton.vue'
import HeadingTitle from '@/components/HeadingTitle.vue'
import { useConfigurationStore } from '@/stores/configuration'
import { useLibraryStore } from '@/stores/library'
import { useUIStore } from '@/stores/ui'

export default {
  name: 'PageAbout',
  components: { ContentWithHeading, ControlButton, HeadingTitle },
  setup() {
    return {
      configurationStore: useConfigurationStore(),
      libraryStore: useLibraryStore(),
      uiStore: useUIStore()
    }
  },
  computed: {
    properties() {
      return [
        {
          key: 'property.name',
          value: this.configurationStore.library_name
        },
        {
          key: 'property.artists',
          value: this.$n(this.libraryStore.artists)
        },
        {
          key: 'property.albums',
          value: this.$n(this.libraryStore.albums)
        },
        {
          key: 'property.tracks',
          value: this.$n(this.libraryStore.songs)
        },
        {
          key: 'property.playtime',
          value: this.$filters.toDuration(this.libraryStore.db_playtime)
        },
        {
          alternate: this.$filters.toDateTime(this.libraryStore.updated_at),
          key: 'property.updated',
          value: this.$filters.toRelativeDuration(this.libraryStore.updated_at)
        },
        {
          alternate: this.$filters.toDateTime(this.libraryStore.started_at),
          key: 'property.uptime',
          value: this.$filters.toDurationToNow(this.libraryStore.started_at)
        }
      ]
    }
  },
  methods: {
    openUpdateDialog() {
      this.uiStore.showUpdateDialog = true
    }
  }
}
</script>
