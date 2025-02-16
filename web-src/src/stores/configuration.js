import { defineStore } from 'pinia'

export const useConfigurationStore = defineStore('ConfigurationStore', {
  state: () => ({
    buildoptions: [],
    version: '',
    websocket_port: 0,
    allow_modifying_stored_playlists: false,
    default_playlist_directory: ''
  })
})
