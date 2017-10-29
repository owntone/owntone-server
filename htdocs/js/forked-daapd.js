

var app = new Vue({
  el: '#root',
  data: {
    config: {},
    library: {},
    outputs: [],
    verification_req: { pin: '' },
    spotify: {},
    pairing: {},
    pairing_req: { pin: '' },
    libspotify: { user: '', password: '', errors: { user: '', password: '', error: '' } },
    lastfm: {},
    lastfm_login: { user: '', password: '', errors: { user: '', password: '', error: '' } }
  },

  created: function () {
    this.loadConfig();
    this.loadLibrary();
    this.loadOutputs();
    this.loadSpotify();
    this.loadPairing();
    this.loadLastfm();
  },

  methods: {
    loadConfig: function() {
      axios.get('/api/config').then(response => {
        this.config = response.data;
        this.connect()});
    },

    loadLibrary: function() {
      axios.get('/api/library').then(response => this.library = response.data);
    },

    loadOutputs: function() {
      axios.get('/api/outputs').then(response => this.outputs = response.data.outputs);
    },

    loadSpotify: function() {
      axios.get('/api/spotify').then(response => this.spotify = response.data);
    },

    loadPairing: function() {
      axios.get('/api/pairing').then(response => this.pairing = response.data);
    },

    loadLastfm: function() {
      axios.get('/api/lastfm').then(response => this.lastfm = response.data);
    },

    update: function() {
      this.library.updating = true;
      axios.get('/api/update').then(console.log('Library is updating'));
    },

    kickoffPairing: function() {
      axios.post('/api/pairing', this.pairing_req).then(response => {
        console.log('Kicked off pairing');
        if (!this.config.websocket_port) {
          this.pairing = {};
        }
      });
    },

    kickoffVerification: function() {
      axios.post('/api/verification', this.verification_req).then(response => {
        console.log('Kicked off verification');
        this.verification_req.pin = '';
      });
    },

    selectOutputs: function() {
      var selected_outputs = [];
      for (var i = 0; i < this.outputs.length; i++) {
        if (this.outputs[i].selected) {
          selected_outputs.push(this.outputs[i].id);
        }
      }

      axios.post('/api/select-outputs', { outputs: selected_outputs }).then(response => {
        if (!this.config.websocket_port) {
          this.loadOutputs();
        }
      });
    },

    loginLibspotify: function() {
      axios.post('/api/spotify-login', this.libspotify).then(response => {
        this.libspotify.user = '';
        this.libspotify.password = '';
        this.libspotify.errors.user = '';
        this.libspotify.errors.password = '';
        this.libspotify.errors.error = '';
        if (!response.data.success) {
          this.libspotify.errors.user = response.data.errors.user;
          this.libspotify.errors.password = response.data.errors.password;
          this.libspotify.errors.error = response.data.errors.error;
        }
      });
    },

    loginLastfm: function() {
      axios.post('/api/lastfm-login', this.lastfm_login).then(response => {
        this.lastfm_login.user = '';
        this.lastfm_login.password = '';
        this.lastfm_login.errors.user = '';
        this.lastfm_login.errors.password = '';
        this.lastfm_login.errors.error = '';
        if (!response.data.success) {
          this.lastfm_login.errors.user = response.data.errors.user;
          this.lastfm_login.errors.password = response.data.errors.password;
          this.lastfm_login.errors.error = response.data.errors.error;
        }
      });
    },

    logoutLastfm: function() {
      axios.get('/api/lastfm-logout', this.lastfm_login).then(response => {
        if (!this.config.websocket_port) {
          this.loadLastfm();
        }
      });
    },

    connect: function() {
      if (this.config.websocket_port <= 0) {
        console.log('Websocket disabled');
        return;
      }
      var socket = new WebSocket('ws://' + document.domain + ':' + this.config.websocket_port, 'notify');
      const vm = this;
      socket.onopen = function() {
          socket.send(JSON.stringify({ notify: ['update', 'pairing', 'spotify', 'lastfm', 'outputs']}));
          socket.onmessage = function(response) {
              console.log(response.data); // upon message
              var data = JSON.parse(response.data);
              if (data.notify.includes('update')) {
                vm.loadLibrary();
              }
              if (data.notify.includes('pairing')) {
                vm.loadPairing();
              }
              if (data.notify.includes('spotify')) {
                vm.loadSpotify();
              }
              if (data.notify.includes('lastfm')) {
                vm.loadLastfm();
              }
              if (data.notify.includes('outputs')) {
                vm.loadOutputs();
              }
          };
      };
    }
  },

  filters: {
    duration: function(seconds) {
      // Display seconds as hours:minutes:seconds

      var h = Math.floor(seconds / 3600);
      var m = Math.floor(seconds % 3600 / 60);
      var s = Math.floor(seconds % 3600 % 60);

      return h + ":" + ('0' + m).slice(-2) + ":" + ('0' + s).slice(-2);
    },

    join: function(array) {
      return array.join(', ');
    }
  }

})
