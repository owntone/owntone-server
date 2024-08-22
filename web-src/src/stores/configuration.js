import { defineStore } from 'pinia'

export const useConfigurationStore = defineStore('ConfigurationStore', {
  state: () => ({
    buildoptions: [],
    version: '',
    websocket_port: 0
  })
})
