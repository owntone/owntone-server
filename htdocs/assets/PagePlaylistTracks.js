import{C as g}from"./ContentWithHeading.js";import{G as h}from"./GroupByList.js";import{L as w}from"./ListTracks.js";import{M as C}from"./ModalDialogPlaylist.js";import{_ as b,e as u,r,o as x,c as v,d as l,w as p,a,t as m}from"./index.js";import"./spotify-web-api.js";const c={load(t){return Promise.all([u.library_playlist(t.params.id),u.library_playlist_tracks(t.params.id)])},set(t,s){t.playlist=s[0].data,t.tracks=new h(s[1].data)}},P={name:"PagePlaylistTracks",components:{ContentWithHeading:g,ListTracks:w,ModalDialogPlaylist:C},beforeRouteEnter(t,s,o){c.load(t).then(n=>{o(e=>c.set(e,n))})},beforeRouteUpdate(t,s,o){const n=this;c.load(t).then(e=>{c.set(n,e),o()})},data(){return{playlist:{},show_details_modal:!1,tracks:new h}},computed:{uris(){return this.playlist.random?this.tracks.map(t=>t.uri).join(","):this.playlist.uri}},methods:{play(){u.player_play_uri(this.uris,!0)}}},B=["textContent"],z={class:"buttons is-centered"},L=["textContent"],T=["textContent"];function j(t,s,o,n,e,i){const _=r("mdicon"),y=r("list-tracks"),f=r("modal-dialog-playlist"),k=r("content-with-heading");return x(),v("div",null,[l(k,null,{"heading-left":p(()=>[a("div",{class:"title is-4",textContent:m(e.playlist.name)},null,8,B)]),"heading-right":p(()=>[a("div",z,[a("a",{class:"button is-small is-light is-rounded",onClick:s[0]||(s[0]=d=>e.show_details_modal=!0)},[l(_,{class:"icon",name:"dots-horizontal",size:"16"})]),a("a",{class:"button is-small is-dark is-rounded",onClick:s[1]||(s[1]=(...d)=>i.play&&i.play(...d))},[l(_,{class:"icon",name:"shuffle",size:"16"}),a("span",{textContent:m(t.$t("page.playlist.shuffle"))},null,8,L)])])]),content:p(()=>[a("p",{class:"heading has-text-centered-mobile",textContent:m(t.$t("page.playlist.track-count",{count:e.tracks.count}))},null,8,T),l(y,{tracks:e.tracks,uris:i.uris},null,8,["tracks","uris"]),l(f,{show:e.show_details_modal,playlist:e.playlist,uris:i.uris,onClose:s[2]||(s[2]=d=>e.show_details_modal=!1)},null,8,["show","playlist","uris"])]),_:1})])}const V=b(P,[["render",j]]);export{V as default};