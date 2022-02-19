<template>
  <section class="section fd-tabs-section">
    <div class="container">
      <div class="columns is-centered">
        <div class="column is-four-fifths">
          <div class="tabs is-centered is-small">
            <ul>
              <router-link to="/settings/webinterface" custom v-slot="{ navigate, isActive }">
                <li :class="{'is-active': isActive}">
                  <a @click="navigate" @keypress.enter="navigate">
                    <span class="">Webinterface</span>
                  </a>
                </li>
              </router-link>
              <router-link to="/settings/remotes-outputs" custom v-slot="{ navigate, isActive }">
                <li :class="{'is-active': isActive}">
                  <a @click="navigate" @keypress.enter="navigate">
                    <span class="">Remotes &amp; Outputs</span>
                  </a>
                </li>
              </router-link>
              <router-link to="/settings/artwork" custom v-slot="{ navigate, isActive }">
                <li :class="{'is-active': isActive}">
                  <a @click="navigate" @keypress.enter="navigate">
                    <span class="">Artwork</span>
                  </a>
                </li>
              </router-link>
              <router-link to="/settings/online-services" custom v-slot="{ navigate, isActive }">
                <li :class="{'is-active': isActive}">
                  <a @click="navigate" @keypress.enter="navigate">
                    <span class="">Online Services</span>
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
  name: 'TabsSettings',

  computed: {
  }
}
</script>

<style>
</style>
