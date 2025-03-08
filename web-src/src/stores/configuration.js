import { defineStore } from 'pinia'

export const useConfigurationStore = defineStore('ConfigurationStore', {
  state: () => ({
    allow_modifying_stored_playlists: false,
    buildoptions: [],
    default_playlist_directory: '',
    library_name: '',
    version: '',
    websocket_port: 0
  })
})
