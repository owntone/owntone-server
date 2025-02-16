<template>
  <modal-dialog
    :actions="actions"
    :show="show"
    :title="$t('dialog.add.stream.title')"
    @close="$emit('close')"
  >
    <template #content>
      <form @submit.prevent="play">
        <div class="field">
          <p class="control has-icons-left">
            <input
              ref="url_field"
              v-model="url"
              class="input"
              type="url"
              pattern="http[s]?://.+"
              required
              :placeholder="$t('dialog.add.stream.placeholder')"
              :disabled="loading"
              @input="check_url"
            />
            <mdicon class="icon is-left" name="web" size="16" />
          </p>
        </div>
      </form>
    </template>
  </modal-dialog>
</template>

<script>
import ModalDialog from '@/components/ModalDialog.vue'
import webapi from '@/webapi'

export default {
  name: 'ModalDialogAddStream',
  components: { ModalDialog },
  props: { show: Boolean },
  emits: ['close'],
  data() {
    return {
      disabled: true,
      loading: false,
      url: ''
    }
  },
  computed: {
    actions() {
      if (this.loading) {
        return [{ label: this.$t('dialog.add.stream.processing'), icon: 'web' }]
      }
      return [
        {
          label: this.$t('dialog.add.stream.cancel'),
          handler: this.cancel,
          icon: 'cancel'
        },
        {
          label: this.$t('dialog.add.stream.add'),
          disabled: this.disabled,
          handler: this.add,
          icon: 'playlist-plus'
        },
        {
          label: this.$t('dialog.add.stream.play'),
          disabled: this.disabled,
          handler: this.play,
          icon: 'play'
        }
      ]
    }
  },
  watch: {
    show() {
      if (this.show) {
        this.loading = false
        // Delay setting the focus on the input field until it is part of the DOM and visible
        setTimeout(() => {
          this.$refs.url_field.focus()
        }, 10)
      }
    }
  },
  methods: {
    add() {
      this.loading = true
      webapi
        .queue_add(this.url)
        .then(() => {
          this.$emit('close')
          this.url = ''
        })
        .catch(() => {
          this.loading = false
        })
    },
    cancel() {
      this.$emit('close')
    },
    check_url(event) {
      const { validity } = event.target
      this.disabled = validity.patternMismatch || validity.valueMissing
    },

    play() {
      this.loading = true
      webapi
        .player_play_uri(this.url, false)
        .then(() => {
          this.$emit('close')
          this.url = ''
        })
        .catch(() => {
          this.loading = false
        })
    }
  }
}
</script>
