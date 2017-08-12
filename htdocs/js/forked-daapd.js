

var app = new Vue({
  el: '#root',
  data: {
    config: {},
    library: {},
    spotify: {},
    pairing: {},
    pairing_req: { pin: '' },
    libspotify: { user: '', password: '', errors: { user: '', password: '', error: '' } }
  },

  created: function () {
    this.loadConfig();
    this.loadLibrary();
    this.loadSpotify();
    this.loadPairing();
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

    loadSpotify: function() {
      axios.get('/api/spotify').then(response => this.spotify = response.data);
    },

    loadPairing: function() {
      axios.get('/api/pairing').then(response => this.pairing = response.data);
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

    connect: function() {
      if (this.config.websocket_port <= 0) {
        console.log('Websocket disabled');
        return;
      }
      var socket = new WebSocket('ws://' + document.domain + ':' + this.config.websocket_port, 'notify');
      const vm = this;
      socket.onopen = function() {
          socket.send(JSON.stringify({ notify: ['update', 'pairing', 'spotify']}));
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
