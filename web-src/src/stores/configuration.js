import configuration from '@/api/configuration'
import { defineStore } from 'pinia'

export const useConfigurationStore = defineStore('ConfigurationStore', {
  actions: {
    async initialise() {
      this.$state = await configuration.state()
    }
  },
  state: () => ({
    allow_modifying_stored_playlists: false,
    buildoptions: [],
    default_playlist_directory: '',
    directories: [],
    hide_singles: false,
    library_name: '',
    radio_playlists: false,
    version: '',
    websocket_port: 0
  })
})
