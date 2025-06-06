import { defineStore } from 'pinia'
import i18n from '@/i18n'
import settings from '@/api/settings'

const { t, availableLocales } = i18n.global

export const useSettingsStore = defineStore('SettingsStore', {
  actions: {
    currentAppearance() {
      return this.$state.appearance
    },
    currentLocale() {
      const languages = availableLocales
      let locale = languages.find((lang) => lang === i18n.global.locale.value)
      const [partial] = i18n.global.locale.value.split('-')
      if (!locale) {
        locale = languages.find((lang) => lang === partial)
      }
      if (!locale) {
        locale = languages.find((lang) => lang.split('-')[0] === partial)
      }
      if (!locale) {
        locale = i18n.global.fallbackLocale
      }
      return locale
    },
    get(categoryName, optionName) {
      return (
        this.categories
          .find((category) => category.name === categoryName)
          ?.options.find((option) => option.name === optionName) ?? {}
      )
    },
    async initialise() {
      this.$state = await settings.state()
    },
    setAppearance(appearance) {
      this.$state.appearance = appearance
      if (appearance === 'dark') {
        document.documentElement.setAttribute('data-theme', 'dark')
      } else if (appearance === 'light') {
        document.documentElement.setAttribute('data-theme', 'light')
      } else {
        document.documentElement.removeAttribute('data-theme')
      }
    },
    setLocale(locale) {
      i18n.global.locale.value = locale
    },
    update(option) {
      const settingCategory = this.categories.find(
        (category) => category.name === option.category
      )
      if (!settingCategory) {
        return
      }
      const settingOption = settingCategory.options.find(
        (setting) => setting.name === option.name
      )
      if (settingOption) {
        settingOption.value = option.value
      }
    }
  },
  getters: {
    appearances() {
      return [
        { id: 'auto', name: t('settings.appearance.auto') },
        { id: 'light', name: t('settings.appearance.light') },
        { id: 'dark', name: t('settings.appearance.dark') }
      ]
    },
    locales() {
      return availableLocales.map((item) => ({
        id: item,
        name: t(`language.${item}`)
      }))
    },
    recentlyAddedLimit: (state) =>
      state.get('webinterface', 'recently_added_limit')?.value ?? 100,
    showComposerForGenre: (state) =>
      state.get('webinterface', 'show_composer_for_genre')?.value ?? null,
    showComposerNowPlaying: (state) =>
      state.get('webinterface', 'show_composer_now_playing')?.value ?? false,
    showCoverArtworkInAlbumLists: (state) =>
      state.get('artwork', 'show_cover_artwork_in_album_lists')?.value ?? false,
    showFilepathNowPlaying: (state) =>
      state.get('webinterface', 'show_filepath_now_playing')?.value ?? false,
    showMenuItemAudiobooks: (state) =>
      state.get('webinterface', 'show_menu_item_audiobooks')?.value ?? false,
    showMenuItemFiles: (state) =>
      state.get('webinterface', 'show_menu_item_files')?.value ?? false,
    showMenuItemMusic: (state) =>
      state.get('webinterface', 'show_menu_item_music')?.value ?? false,
    showMenuItemPlaylists: (state) =>
      state.get('webinterface', 'show_menu_item_playlists')?.value ?? false,
    showMenuItemPodcasts: (state) =>
      state.get('webinterface', 'show_menu_item_podcasts')?.value ?? false,
    showMenuItemRadio: (state) =>
      state.get('webinterface', 'show_menu_item_radio')?.value ?? false,
    showMenuItemSearch: (state) =>
      state.get('webinterface', 'show_menu_item_search')?.value ?? false
  },
  state: () => ({ appearance: 'auto', categories: [] })
})
