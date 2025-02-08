<template>
  <modal-dialog
    :actions="actions"
    :show="show"
    @add="add"
    @cancel="$emit('close')"
    @close="$emit('close')"
    @play="play"
  >
    <template #modal-content>
      <form @submit.prevent="play">
        <p class="title is-4" v-text="$t('dialog.add.stream.title')" />
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
  name: 'ModalDialogAddUrlStream',
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
          event: 'cancel',
          icon: 'cancel'
        },
        {
          label: this.$t('dialog.add.stream.add'),
          disabled: this.disabled,
          event: 'add',
          icon: 'playlist-plus'
        },
        {
          label: this.$t('dialog.add.stream.play'),
          disabled: this.disabled,
          event: 'play',
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
