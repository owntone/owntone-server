<template>
  <section class="section fd-tabs-section">
    <div class="container">
      <div class="columns is-centered">
        <div class="column is-four-fifths">
          <div class="tabs is-centered is-small">
            <ul>
              <router-link
                v-slot="{ navigate, isActive }"
                :to="{ name: 'settings-webinterface' }"
                custom
              >
                <li :class="{ 'is-active': isActive }">
                  <a @click="navigate" @keypress.enter="navigate">
                    <span v-text="$t('page.settings.tabs.general')" />
                  </a>
                </li>
              </router-link>
              <router-link
                v-slot="{ navigate, isActive }"
                :to="{ name: 'settings-remotes-outputs' }"
                custom
              >
                <li :class="{ 'is-active': isActive }">
                  <a @click="navigate" @keypress.enter="navigate">
                    <span
                      v-text="$t('page.settings.tabs.remotes-and-outputs')"
                    />
                  </a>
                </li>
              </router-link>
              <router-link
                v-slot="{ navigate, isActive }"
                :to="{ name: 'settings-artwork' }"
                custom
              >
                <li :class="{ 'is-active': isActive }">
                  <a @click="navigate" @keypress.enter="navigate">
                    <span v-text="$t('page.settings.tabs.artwork')" />
                  </a>
                </li>
              </router-link>
              <router-link
                v-slot="{ navigate, isActive }"
                :to="{ name: 'settings-online-services' }"
                custom
              >
                <li :class="{ 'is-active': isActive }">
                  <a @click="navigate" @keypress.enter="navigate">
                    <span v-text="$t('page.settings.tabs.online-services')" />
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
  name: 'TabsSettings'
}
</script>

<style></style>
