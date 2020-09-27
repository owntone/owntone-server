<template>
  <section class="section fd-tabs-section">
    <div class="container">
      <div class="columns is-centered">
        <div class="column is-four-fifths">
          <div class="tabs is-centered is-small">
            <ul>
              <router-link tag="li" to="/audiobooks/artists" active-class="is-active">
                <a>
                  <span class="icon is-small"><i class="mdi mdi-artist"></i></span>
                  <span class="">Authors</span>
                </a>
              </router-link>
              <router-link tag="li" to="/audiobooks/albums" active-class="is-active">
                <a>
                  <span class="icon is-small"><i class="mdi mdi-album"></i></span>
                  <span class="">Audiobooks</span>
                </a>
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
