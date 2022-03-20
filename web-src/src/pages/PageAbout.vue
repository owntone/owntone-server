<template>
  <div>
    <section class="section">
      <div class="container">
        <div class="columns is-centered">
          <div class="column is-four-fifths has-text-centered-mobile">
            <p class="heading"><b>OwnTone</b> - version {{ config.version }}</p>
            <h1 class="title is-4">
              {{ config.library_name }}
            </h1>
          </div>
        </div>
      </div>
    </section>
    <section class="section">
      <div class="container">
        <div class="columns is-centered">
          <div class="column is-four-fifths">
            <div class="content">
              <nav class="level is-mobile">
                <!-- Left side -->
                <div class="level-left">
                  <div class="level-item">
                    <h2 class="title is-5">Library</h2>
                  </div>
                </div>

                <!-- Right side -->
                <div class="level-right">
                  <div v-if="library.updating">
                    <a class="button is-small is-loading">Update</a>
                  </div>
                  <div v-else>
                    <a class="button is-small" @click="showUpdateDialog()"
                      >Update</a
                    >
                  </div>
                </div>
              </nav>

              <table class="table">
                <tbody>
                  <tr>
                    <th>Artists</th>
                    <td class="has-text-right">
                      {{ $filters.number(library.artists) }}
                    </td>
                  </tr>
                  <tr>
                    <th>Albums</th>
                    <td class="has-text-right">
                      {{ $filters.number(library.albums) }}
                    </td>
                  </tr>
                  <tr>
                    <th>Tracks</th>
                    <td class="has-text-right">
                      {{ $filters.number(library.songs) }}
                    </td>
                  </tr>
                  <tr>
                    <th>Total playtime</th>
                    <td class="has-text-right">
                      {{
                        $filters.duration(
                          library.db_playtime * 1000,
                          'y [years], d [days], h [hours], m [minutes]'
                        )
                      }}
                    </td>
                  </tr>
                  <tr>
                    <th>Library updated</th>
                    <td class="has-text-right">
                      {{ $filters.timeFromNow(library.updated_at) }}
                      <span class="has-text-grey"
                        >({{ $filters.time(library.updated_at, 'lll') }})</span
                      >
                    </td>
                  </tr>
                  <tr>
                    <th>Uptime</th>
                    <td class="has-text-right">
                      {{ $filters.timeFromNow(library.started_at, true) }}
                      <span class="has-text-grey"
                        >({{ $filters.time(library.started_at, 'll') }})</span
                      >
                    </td>
                  </tr>
                </tbody>
              </table>
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
              <p class="is-size-7">
                Compiled with support for {{ config.buildoptions.join(', ') }}.
              </p>
              <p class="is-size-7">
                Web interface built with <a href="http://bulma.io">Bulma</a>,
                <a href="https://materialdesignicons.com/"
                  >Material Design Icons</a
                >, <a href="https://vuejs.org/">Vue.js</a>,
                <a href="https://github.com/mzabriskie/axios">axios</a> and
                <a
                  href="https://github.com/owntone/owntone-server/network/dependencies"
                  >more</a
                >.
              </p>
            </div>
          </div>
        </div>
      </div>
    </section>
  </div>
</template>

<script>
import * as types from '@/store/mutation_types'

export default {
  name: 'PageAbout',

  data() {
    return {
      show_update_dropdown: false,
      show_update_library: false
    }
  },

  computed: {
    config() {
      return this.$store.state.config
    },
    library() {
      return this.$store.state.library
    }
  },

  methods: {
    onClickOutside(event) {
      this.show_update_dropdown = false
    },
    showUpdateDialog() {
      this.$store.commit(types.SHOW_UPDATE_DIALOG, true)
    }
  }
}
</script>

<style></style>
