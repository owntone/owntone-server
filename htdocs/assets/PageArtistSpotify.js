import{C as E}from"./ContentWithHeading.js";import{C as V}from"./CoverArtwork.js";import{L}from"./ListItemAlbumSpotify.js";import{M}from"./ModalDialogAlbumSpotify.js";import{M as z}from"./ModalDialogArtistSpotify.js";import{S as w}from"./spotify-web-api.js";import{_ as B,e as N,r as l,o as d,c as h,d as n,w as m,s as b,a as r,t as p,F as P,f as D,g,m as T,h as F,b as G,k as I}from"./index.js";import{G as R}from"./vue-eternal-loading.js";const f=50,u={load(t){const e=new w;return e.setAccessToken(b.state.spotify.webapi_token),Promise.all([e.getArtist(t.params.id),e.getArtistAlbums(t.params.id,{limit:f,offset:0,include_groups:"album,single",market:b.state.spotify.webapi_country})])},set(t,e){t.artist=e[0],t.albums=[],t.total=0,t.offset=0,t.append_albums(e[1])}},W={name:"PageArtistSpotify",components:{ContentWithHeading:E,CoverArtwork:V,ListItemAlbumSpotify:L,ModalDialogAlbumSpotify:M,ModalDialogArtistSpotify:z,VueEternalLoading:R},beforeRouteEnter(t,e,a){u.load(t).then(_=>{a(s=>u.set(s,_))})},beforeRouteUpdate(t,e,a){const _=this;u.load(t).then(s=>{u.set(_,s),a()})},data(){return{albums:[],artist:{},offset:0,selected_album:{},show_album_details_modal:!1,show_details_modal:!1,total:0}},computed:{is_visible_artwork(){return this.$store.getters.settings_option("webinterface","show_cover_artwork_in_album_lists").value}},methods:{load_next({loaded:t}){const e=new w;e.setAccessToken(this.$store.state.spotify.webapi_token),e.getArtistAlbums(this.artist.id,{limit:f,offset:this.offset,include_groups:"album,single"}).then(a=>{this.append_albums(a),t(a.items.length,f)})},append_albums(t){this.albums=this.albums.concat(t.items),this.total=t.total,this.offset+=t.limit},play(){this.show_album_details_modal=!1,N.player_play_uri(this.artist.uri,!0)},open_album(t){this.$router.push({name:"music-spotify-album",params:{id:t.id}})},open_dialog(t){this.selected_album=t,this.show_album_details_modal=!0},artwork_url(t){return t.images&&t.images.length>0?t.images[0].url:""}}},j=["textContent"],H={class:"buttons is-centered"},O=["textContent"],U=["textContent"],Z=["onClick"];function q(t,e,a,_,s,i){const c=l("mdicon"),k=l("cover-artwork"),y=l("list-item-album-spotify"),C=l("VueEternalLoading"),A=l("modal-dialog-artist-spotify"),v=l("modal-dialog-album-spotify"),x=l("content-with-heading");return d(),h("div",null,[n(x,null,{"heading-left":m(()=>[r("p",{class:"title is-4",textContent:p(s.artist.name)},null,8,j)]),"heading-right":m(()=>[r("div",H,[r("a",{class:"button is-small is-light is-rounded",onClick:e[0]||(e[0]=o=>s.show_details_modal=!0)},[n(c,{class:"icon",name:"dots-horizontal",size:"16"})]),r("a",{class:"button is-small is-dark is-rounded",onClick:e[1]||(e[1]=(...o)=>i.play&&i.play(...o))},[n(c,{class:"icon",name:"shuffle",size:"16"}),r("span",{textContent:p(t.$t("page.spotify.artist.shuffle"))},null,8,O)])])]),content:m(()=>[r("p",{class:"heading has-text-centered-mobile",textContent:p(t.$t("page.spotify.artist.album-count",{count:s.total}))},null,8,U),(d(!0),h(P,null,D(s.albums,o=>(d(),g(y,{key:o.id,album:o,onClick:S=>i.open_album(o)},T({actions:m(()=>[r("a",{onClick:F(S=>i.open_dialog(o),["prevent","stop"])},[n(c,{class:"icon has-text-dark",name:"dots-vertical",size:"16"})],8,Z)]),_:2},[i.is_visible_artwork?{name:"artwork",fn:m(()=>[n(k,{artwork_url:i.artwork_url(o),artist:o.artist,album:o.name,class:"is-clickable fd-has-shadow fd-cover fd-cover-small-image",maxwidth:64,maxheight:64},null,8,["artwork_url","artist","album"])]),key:"0"}:void 0]),1032,["album","onClick"]))),128)),s.offset<s.total?(d(),g(C,{key:0,load:i.load_next},{"no-more":m(()=>[G(" . ")]),_:1},8,["load"])):I("",!0),n(A,{show:s.show_details_modal,artist:s.artist,onClose:e[2]||(e[2]=o=>s.show_details_modal=!1)},null,8,["show","artist"]),n(v,{show:s.show_album_details_modal,album:s.selected_album,onClose:e[3]||(e[3]=o=>s.show_album_details_modal=!1)},null,8,["show","album"])]),_:1})])}const st=B(W,[["render",q]]);export{st as default};