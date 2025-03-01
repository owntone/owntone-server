<template>
  <section class="section">
    <div class="container">
      <div class="columns is-centered">
        <div class="column is-four-fifths">
          <div class="content">
            <nav class="level">
              <div class="level-left">
                <div class="level-item">
                  <p class="title is-4" v-text="$t('page.about.library')" />
                </div>
              </div>
              <div class="level-right">
                <control-button
                  :class="{ 'is-loading': libraryStore.updating }"
                  :disabled="libraryStore.updating"
                  :handler="showUpdateDialog"
                  icon="refresh"
                  label="page.about.update"
                />
              </div>
            </nav>
            <div
              v-for="property in properties"
              :key="property.label"
              class="media is-align-items-center mb-0"
            >
              <div
                class="media-content has-text-weight-bold"
                v-text="$t(property.label)"
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
          </div>
        </div>
      </div>
    </div>
  </section>
  <section class="section">
    <div class="container">
      <div class="columns is-centered">
        <div class="column is-four-fifths">
          <div class="content has-text-centered-mobile">
            <p
              class="is-size-7"
              v-text="
                $t('page.about.version', {
                  version: configurationStore.version
                })
              "
            />
            <p
              class="is-size-7"
              v-text="
                $t('page.about.compiled-with', {
                  options: configurationStore.buildoptions.join(', ')
                })
              "
            />
            <i18n-t
              tag="p"
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
          </div>
        </div>
      </div>
    </div>
  </section>
</template>

<script>
import ControlButton from '@/components/ControlButton.vue'
import { useConfigurationStore } from '@/stores/configuration'
import { useLibraryStore } from '@/stores/library'
import { useUIStore } from '@/stores/ui'

export default {
  name: 'PageAbout',
  components: { ControlButton },
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
          label: 'property.name',
          value: this.configurationStore.library_name
        },
        {
          label: 'property.artists',
          value: this.$n(this.libraryStore.artists)
        },
        {
          label: 'property.albums',
          value: this.$n(this.libraryStore.albums)
        },
        {
          label: 'property.tracks',
          value: this.$n(this.libraryStore.songs)
        },
        {
          label: 'property.playtime',
          value: this.$filters.toDuration(this.libraryStore.db_playtime)
        },
        {
          label: 'property.updated',
          value: this.$filters.toRelativeDuration(this.libraryStore.updated_at),
          alternate: this.$filters.toDateTime(this.libraryStore.updated_at)
        },
        {
          label: 'property.uptime',
          value: this.$filters.toDurationToNow(this.libraryStore.started_at),
          alternate: this.$filters.toDateTime(this.libraryStore.started_at)
        }
      ]
    }
  },
  methods: {
    showUpdateDialog() {
      this.uiStore.show_update_dialog = true
    }
  }
}
</script>
