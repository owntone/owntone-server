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
                <div v-if="library.updating">
                  <a
                    class="button is-small is-rounded is-loading"
                    v-text="$t('page.about.update')"
                  />
                </div>
                <div v-else>
                  <a
                    class="button is-small is-rounded"
                    @click="showUpdateDialog()"
                    v-text="$t('page.about.update')"
                  />
                </div>
              </div>
            </nav>
            <div class="media">
              <div
                class="media-content has-text-weight-bold"
                v-text="$t('page.about.name')"
              />
              <div class="media-right" v-text="configuration.library_name" />
            </div>
            <div class="media">
              <div
                class="media-content has-text-weight-bold"
                v-text="$t('page.about.artists')"
              />
              <div
                class="media-right"
                v-text="$filters.number(library.artists)"
              />
            </div>
            <div class="media">
              <div
                class="media-content has-text-weight-bold"
                v-text="$t('page.about.albums')"
              />
              <div
                media="media-right"
                v-text="$filters.number(library.albums)"
              />
            </div>
            <div class="media">
              <div
                class="media-content has-text-weight-bold"
                v-text="$t('page.about.tracks')"
              />
              <div
                class="media-right"
                v-text="$filters.number(library.songs)"
              />
            </div>
            <div class="media">
              <div
                class="media-content has-text-weight-bold"
                v-text="$t('page.about.total-playtime')"
              />
              <div
                class="media-right"
                v-text="$filters.durationInDays(library.db_playtime)"
              />
            </div>
            <div class="media">
              <div
                class="media-content has-text-weight-bold"
                v-text="$t('page.about.updated')"
              />
              <div class="media-right">
                <span
                  v-text="
                    $t('page.about.updated-on', {
                      time: $filters.timeFromNow(library.updated_at)
                    })
                  "
                />
                (<span
                  class="has-text-grey"
                  v-text="$filters.datetime(library.updated_at)"
                />)
              </div>
            </div>
            <div class="media">
              <div
                class="media-content has-text-weight-bold"
                v-text="$t('page.about.uptime')"
              />
              <div class="media-right">
                <span v-text="$filters.timeFromNow(library.started_at, true)" />
                (<span
                  class="has-text-grey"
                  v-text="$filters.datetime(library.started_at)"
                />)
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
                $t('page.about.version', { version: configuration.version })
              "
            />
            <p
              class="is-size-7"
              v-text="
                $t('page.about.compiled-with', {
                  options: configuration.buildoptions.join(', ')
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
import { useConfigurationStore } from '@/stores/configuration'
import { useLibraryStore } from '@/stores/library'
import { useUIStore } from '@/stores/ui'

export default {
  name: 'PageAbout',

  setup() {
    return {
      configurationStore: useConfigurationStore(),
      libraryStore: useLibraryStore(),
      uiStore: useUIStore()
    }
  },

  computed: {
    configuration() {
      return this.configurationStore.$state
    },
    library() {
      return this.libraryStore.$state
    }
  },

  methods: {
    showUpdateDialog() {
      this.uiStore.show_update_dialog = true
    }
  }
}
</script>
