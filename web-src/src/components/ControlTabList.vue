<template>
  <section class="section py-0 tabs-section">
    <div class="container">
      <div class="columns is-centered my-0">
        <div class="column is-four-fifths py-0">
          <div class="tabs is-centered is-small">
            <ul>
              <router-link
                v-for="link in links"
                :key="link.to.name"
                v-slot="{ navigate, isActive }"
                :to="{ name: link.to.name }"
                custom
              >
                <li :class="{ 'is-active': isActive }">
                  <a @click="navigate" @keypress.enter="navigate">
                    <mdicon
                      v-if="link.icon"
                      class="icon is-small mx-1"
                      :name="link.icon"
                    />
                    <span
                      :class="{ 'is-hidden-mobile': link.icon }"
                      v-text="$t(link.key)"
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
  name: 'ControlTabList',
  props: { links: { required: true, type: Array } }
}
</script>

<style scoped>
.tabs-section {
  background: var(--bulma-body-background-color);
  position: sticky;
  top: var(--bulma-navbar-height);
  width: 100%;
  z-index: 1;
}
</style>
