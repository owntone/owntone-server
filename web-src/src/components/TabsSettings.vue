<template>
  <section class="section fd-tabs-section">
    <div class="container">
      <div class="columns is-centered">
        <div class="column is-four-fifths">
          <div class="tabs is-centered is-small">
            <ul>
              <router-link tag="li" to="/settings/webinterface" active-class="is-active">
                <a>
                  <span class="">Webinterface</span>
                </a>
              </router-link>
              <router-link tag="li" to="/settings/remotes-outputs" active-class="is-active">
                <a>
                  <span class="">Remotes &amp; Outputs</span>
                </a>
              </router-link>
              <router-link tag="li" to="/settings/artwork" active-class="is-active">
                <a>
                  <span class="">Artwork</span>
                </a>
              </router-link>
              <router-link tag="li" to="/settings/online-services" active-class="is-active">
                <a>
                  <span class="">Online Services</span>
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
  name: 'TabsSettings',

  computed: {
  }
}
</script>

<style>
</style>
