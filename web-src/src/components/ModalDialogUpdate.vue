<template>
  <modal-dialog
      :show="show"
      title="Update library"
      :ok_action="library.updating ? '' : 'Rescan'"
      close_action="Close"
      @ok="update_library"
      @close="close()">
    <template v-slot:modal-content>
      <div v-if="!library.updating">
        <p class="mb-3">Scan for new, deleted and modified files</p>
        <div class="field" v-if="spotify_enabled || rss.tracks > 0">
          <div class="control">
            <div class="select is-small">
              <select v-model="update_dialog_scan_kind">
                <option value="">Update everything</option>
                <option value="files">Only update local library</option>
                <option value="spotify" v-if="spotify_enabled">Only update Spotify</option>
                <option value="rss" v-if="rss.tracks > 0">Only update RSS feeds</option>
              </select>
            </div>
          </div>
        </div>
        <div class="field">
          <label class="checkbox is-size-7 is-small">
            <input type="checkbox" v-model="rescan_metadata">
            Rescan metadata for unmodified files
          </label>
        </div>
      </div>
      <div v-else>
        <p class="mb-3">Library update in progress ...</p>
      </div>
    </template>
  </modal-dialog>
</template>

<script>
import ModalDialog from '@/components/ModalDialog.vue'
import * as types from '@/store/mutation_types'
import webapi from '@/webapi'

export default {
  name: 'ModalDialogUpdate',
  components: { ModalDialog },
  props: ['show'],

  data () {
    return {
      rescan_metadata: false
    }
  },

  computed: {
    library () {
      return this.$store.state.library
    },

    rss () {
      return this.$store.state.rss_count
    },

    spotify_enabled () {
      return this.$store.state.spotify.webapi_token_valid
    },

    update_dialog_scan_kind: {
      get () {
        return this.$store.state.update_dialog_scan_kind
      },
      set (value) {
        this.$store.commit(types.UPDATE_DIALOG_SCAN_KIND, value)
      }
    }
  },

  methods: {
    update_library () {
      if (this.rescan_metadata) {
        webapi.library_rescan(this.update_dialog_scan_kind)
      } else {
        webapi.library_update(this.update_dialog_scan_kind)
      }
    },

    close () {
      this.update_dialog_scan_kind = ''
      this.$emit('close')
    }
  }
}
</script>

<style>
</style>
