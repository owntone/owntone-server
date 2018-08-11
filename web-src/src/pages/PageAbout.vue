<template>
  <div>
    <section class="section">
      <div class="container">
        <div class="columns is-centered">
          <div class="column is-four-fifths has-text-centered-mobile">
            <p class="heading"><b>forked-daapd</b> - version {{ config.version }}</p>
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
                  <a class="button is-small is-outlined is-link" :class="{ 'is-loading': library.updating }" @click="update">Update</a>
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
                    <td class="has-text-right">{{ library.updated_at | timeFromNow }} <span class="has-text-grey">({{ library.updated_at | time('MMM Do, h:mm') }})</span></td>
                  </tr>
                  <tr>
                    <th>Uptime</th>
                    <td class="has-text-right">{{ library.started_at | timeFromNow(true) }} <span class="has-text-grey">({{ library.started_at | time('MMM Do, h:mm') }})</span></td>
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
              <p class="is-size-7"><a href="https://github.com/chme/forked-daapd-web">Web interface</a> v{{ version }} built with <a href="http://bulma.io">Bulma</a>, <a href="https://materialdesignicons.com/">Material Design Icons</a>, <a href="https://vuejs.org/">Vue.js</a>, <a href="https://github.com/mzabriskie/axios">axios</a> and <a href="https://github.com/chme/forked-daapd-web/network/dependencies">more</a>.</p>
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
      'version': process.env.V2
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
    update: function () {
      webapi.library_update()
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
