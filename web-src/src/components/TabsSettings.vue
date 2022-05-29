<template>
  <section class="section fd-tabs-section">
    <div class="container">
      <div class="columns is-centered">
        <div class="column is-four-fifths">
          <div class="tabs is-centered is-small">
            <ul>
              <router-link
                v-slot="{ navigate, isActive }"
                to="/settings/webinterface"
                custom
              >
                <li :class="{ 'is-active': isActive }">
                  <a @click="navigate" @keypress.enter="navigate">
                    <span v-text="$t('page.settings.tabs.settings.general')" />
                  </a>
                </li>
              </router-link>
              <router-link
                v-slot="{ navigate, isActive }"
                to="/settings/remotes-outputs"
                custom
              >
                <li :class="{ 'is-active': isActive }">
                  <a @click="navigate" @keypress.enter="navigate">
                    <span
                      v-text="
                        $t('page.settings.tabs.settings.remotes-and-outputs')
                      "
                    />
                  </a>
                </li>
              </router-link>
              <router-link
                v-slot="{ navigate, isActive }"
                to="/settings/artwork"
                custom
              >
                <li :class="{ 'is-active': isActive }">
                  <a @click="navigate" @keypress.enter="navigate">
                    <span v-text="$t('page.settings.tabs.settings.artwork')" />
                  </a>
                </li>
              </router-link>
              <router-link
                v-slot="{ navigate, isActive }"
                to="/settings/online-services"
                custom
              >
                <li :class="{ 'is-active': isActive }">
                  <a @click="navigate" @keypress.enter="navigate">
                    <span
                      v-text="$t('page.settings.tabs.settings.online-services')"
                    />
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
