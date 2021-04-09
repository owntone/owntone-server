# OwnTone player web interface

Mobile friendly player web interface for [OwnTone](http://owntone.github.io/owntone-server/) build with [Vue.js](https://vuejs.org), [Bulma](http://bulma.io).

## Screenshots

<img src="screenshots/Screenshot-now-playing.png" width="300" alt="Now playing"> <img src="screenshots/Screenshot-queue.png" width="300" alt="Queue"> <img src="screenshots/Screenshot-artists.png" width="300" alt="Artists"> <img src="screenshots/Screenshot-album.png" width="300" alt="Album">


## Usage

You can find OwnTone's web interface at [http://owntone.local:3689](http://owntone.local:3689)
or alternatively at [http://[your_server_address_here]:3689](http://[your_server_address_here]:3689).


## Build Setup

The source is located in the `web-src` folder.

```
cd web-src
```

It is based on the Vue.js webpack template. For a detailed explanation on how things work, check out the [guide](http://vuejs-templates.github.io/webpack/) and [docs for vue-loader](http://vuejs.github.io/vue-loader).

``` bash
# install dependencies
npm install

# serve with hot reload at localhost:8080
npm run dev

# build for production with minification (will update player web interface in "../htdocs")
npm run build

# build for production and view the bundle analyzer report
npm run build --report
```

After running `npm run dev` the web interface is reachable at [localhost:8080](http://localhost:8080). By default it expects **owntone** to be running at [localhost:3689](http://localhost:3689) and proxies all JSON API calls to this location. If the server is running at a different location you need to modify the `proxyTable` configuration in `config/index.js`
