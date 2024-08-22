import { defineStore } from 'pinia'

export const useUIStore = defineStore('UIStore', {
  state: () => ({
    albums_sort: 1,
    artist_albums_sort: 1,
    artist_tracks_sort: 1,
    artists_sort: 1,
    composer_tracks_sort: 1,
    genre_tracks_sort: 1,
    hide_singles: false,
    hide_spotify: false,
    show_burger_menu: false,
    show_only_next_items: false,
    show_player_menu: false,
    show_update_dialog: false
  })
})
