<template>
  <section class="section fd-tabs-section">
    <div class="container">
      <div class="columns is-centered">
        <div class="column is-four-fifths">
          <div class="tabs is-centered is-small">
            <ul>
              <router-link
                v-slot="{ navigate, isActive }"
                :to="{ name: 'audiobooks-artists' }"
                custom
              >
                <li :class="{ 'is-active': isActive }">
                  <a @click="navigate" @keypress.enter="navigate">
                    <mdicon
                      class="icon is-small"
                      name="account-music"
                      size="16"
                    />
                    <span v-text="$t('page.audiobooks.tabs.authors')" />
                  </a>
                </li>
              </router-link>
              <router-link
                v-slot="{ navigate, isActive }"
                :to="{ name: 'audiobooks-albums' }"
                custom
              >
                <li :class="{ 'is-active': isActive }">
                  <a @click="navigate" @keypress.enter="navigate">
                    <mdicon class="icon is-small" name="album" size="16" />
                    <span v-text="$t('page.audiobooks.tabs.audiobooks')" />
                  </a>
                </li>
              </router-link>
              <router-link
                v-slot="{ navigate, isActive }"
                :to="{ name: 'audiobooks-genres' }"
                custom
              >
                <li :class="{ 'is-active': isActive }">
                  <a @click="navigate" @keypress.enter="navigate">
                    <mdicon class="icon is-small" name="speaker" size="16" />
                    <span v-text="$t('page.audiobooks.tabs.genres')" />
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

<style></style>
