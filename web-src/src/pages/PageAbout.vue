<template>
  <div>
    <section class="section">
      <div class="container">
        <div class="columns is-centered">
          <div class="column is-four-fifths has-text-centered-mobile">
            <p class="heading"><b>OwnTone</b> - version {{ config.version }}</p>
            <h1 class="title is-4">{{ config.library_name }}</h1>
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
                  <div v-if="library.updating"><a class="button is-small is-loading">Update</a></div>
                  <div v-else class="dropdown is-right" :class="{ 'is-active': show_update_dropdown }" v-click-outside="onClickOutside">
                    <div class="dropdown-trigger">
                      <div class="buttons has-addons">
                        <a @click="update" class="button is-small">Update</a>
                        <a @click="show_update_dropdown = !show_update_dropdown" class="button is-small">
                          <span class="icon"><i class="mdi" :class="{ 'mdi-chevron-down': !show_update_dropdown, 'mdi-chevron-up': show_update_dropdown }"></i></span>
                        </a>
                      </div>
                    </div>
                    <div class="dropdown-menu" id="dropdown-menu" role="menu">
                      <div class="dropdown-content">
                        <div class="dropdown-item">
                          <a @click="update" class="has-text-dark">
                            <strong>Update</strong><br>
                            <span class="is-size-7">Adds new, removes deleted and updates modified files.</span>
                          </a>
                        </div>
                        <hr class="dropdown-divider">
                        <div class="dropdown-item">
                          <a @click="update_meta" class="has-text-dark">
                            <strong>Rescan metadata</strong><br>
                            <span class="is-size-7">Same as update, but also rescans unmodified files.</span>
                          </a>
                        </div>
                      </div>
                    </div>
                  </div>
                </div>
              </nav>

              <table class="table">
                <tbody>
                  <tr>
                    <th>Artists</th>
                    <td class="has-text-right">{{ library.artists | number }}</td>
                  </tr>
                  <tr>
                    <th>Albums</th>
                    <td class="has-text-right">{{ library.albums | number }}</td>
                  </tr>
                  <tr>
                    <th>Tracks</th>
                    <td class="has-text-right">{{ library.songs | number }}</td>
                  </tr>
                  <tr>
                    <th>Total playtime</th>
                    <td class="has-text-right">{{ library.db_playtime * 1000 | duration('y [years], d [days], h [hours], m [minutes]') }}</td>
                  </tr>
                  <tr>
                    <th>Library updated</th>
                    <td class="has-text-right">{{ library.updated_at | timeFromNow }} <span class="has-text-grey">({{ library.updated_at | time('lll') }})</span></td>
                  </tr>
                  <tr>
                    <th>Uptime</th>
                    <td class="has-text-right">{{ library.started_at | timeFromNow(true) }} <span class="has-text-grey">({{ library.started_at | time('ll') }})</span></td>
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
              <p class="is-size-7">Compiled with support for {{ config.buildoptions | join }}.</p>
              <p class="is-size-7">Web interface built with <a href="http://bulma.io">Bulma</a>, <a href="https://materialdesignicons.com/">Material Design Icons</a>, <a href="https://vuejs.org/">Vue.js</a>, <a href="https://github.com/mzabriskie/axios">axios</a> and <a href="https://github.com/ejurgensen/OwnTone/network/dependencies">more</a>.</p>
            </div>
          </div>
        </div>
      </div>
    </section>
  </div>
</template>

<script>
import webapi from '@/webapi'

export default {
  name: 'PageAbout',

  data () {
    return {
      show_update_dropdown: false
    }
  },

  computed: {
    config () {
      return this.$store.state.config
    },
    library () {
      return this.$store.state.library
    }
  },

  methods: {
    onClickOutside (event) {
      this.show_update_dropdown = false
    },

    update: function () {
      this.show_update_dropdown = false
      webapi.library_update()
    },

    update_meta: function () {
      this.show_update_dropdown = false
      webapi.library_rescan()
    }
  },

  filters: {
    join: function (array) {
      return array.join(', ')
    }
  }
}
</script>

<style>
</style>
