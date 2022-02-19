<template>
  <section class="section fd-tabs-section">
    <div class="container">
      <div class="columns is-centered">
        <div class="column is-four-fifths">
          <div class="tabs is-centered is-small">
            <ul>
              <router-link to="/audiobooks/artists" custom v-slot="{ navigate, isActive }">
                <li :class="{'is-active': isActive}">
                  <a @click="navigate" @keypress.enter="navigate">
                    <span class="icon is-small"><i class="mdi mdi-artist"></i></span>
                    <span class="">Authors</span>
                  </a>
                </li>
              </router-link>
              <router-link to="/audiobooks/albums" custom v-slot="{ navigate, isActive }">
                <li :class="{'is-active': isActive}">
                  <a @click="navigate" @keypress.enter="navigate">
                    <span class="icon is-small"><i class="mdi mdi-album"></i></span>
                    <span class="">Audiobooks</span>
                  </a>
                </li>
              </router-link>
            </ul>
          </div>
        </div>
      </div>
    </div>
  </section>
</template>

<script>
export default {
  name: 'TabsAudiobooks'
}
</script>

<style>
</style>
