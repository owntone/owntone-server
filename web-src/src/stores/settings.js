import { defineStore } from 'pinia'

export const useSettingsStore = defineStore('SettingsStore', {
  state: () => ({
    categories: []
  }),
  getters: {
    recently_added_limit: (state) =>
      state.setting('webinterface', 'recently_added_limit')?.value ?? 100,
    show_composer_for_genre: (state) =>
      state.setting('webinterface', 'show_composer_for_genre')?.value ?? null,
    show_composer_now_playing: (state) =>
      state.setting('webinterface', 'show_composer_now_playing')?.value ??
      false,
    show_cover_artwork_in_album_lists: (state) =>
      state.setting('webinterface', 'show_cover_artwork_in_album_lists')
        ?.value ?? false,
    show_filepath_now_playing: (state) =>
      state.setting('webinterface', 'show_filepath_now_playing')?.value ??
      false,
    show_menu_item_audiobooks: (state) =>
      state.setting('webinterface', 'show_menu_item_audiobooks')?.value ??
      false,
    show_menu_item_files: (state) =>
      state.setting('webinterface', 'show_menu_item_files')?.value ?? false,
    show_menu_item_music: (state) =>
      state.setting('webinterface', 'show_menu_item_music')?.value ?? false,
    show_menu_item_playlists: (state) =>
      state.setting('webinterface', 'show_menu_item_playlists')?.value ?? false,
    show_menu_item_podcasts: (state) =>
      state.setting('webinterface', 'show_menu_item_podcasts')?.value ?? false,
    show_menu_item_radio: (state) =>
      state.setting('webinterface', 'show_menu_item_radio')?.value ?? false,
    show_menu_item_search: (state) =>
      state.setting('webinterface', 'show_menu_item_search')?.value ?? false
  },
  actions: {
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
    },
    setting(categoryName, optionName) {
      return (
        this.categories
          .find((category) => category.name === categoryName)
          ?.options.find((option) => option.name === optionName) ?? {}
      )
    }
  }
})
