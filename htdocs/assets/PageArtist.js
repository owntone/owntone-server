import{_ as C,l as w,e as p,r,o as k,c as x,d as i,w as m,a as e,t as l}from"./index.js";import{C as v}from"./ContentWithHeading.js";import{C as A}from"./ControlDropdown.js";import{G as h,a as B,c as L}from"./GroupByList.js";import{L as R}from"./ListAlbums.js";import{M as S}from"./ModalDialogArtist.js";import"./CoverArtwork.js";import"./ModalDialogAlbum.js";const u={load(t){return Promise.all([p.library_artist(t.params.id),p.library_artist_albums(t.params.id)])},set(t,s){t.artist=s[0].data,t.albums_list=new h(s[1].data)}},z={name:"PageArtist",components:{ContentWithHeading:v,ControlDropdown:A,ListAlbums:R,ModalDialogArtist:S},beforeRouteEnter(t,s,d){u.load(t).then(_=>{d(o=>u.set(o,_))})},beforeRouteUpdate(t,s,d){const _=this;u.load(t).then(o=>{u.set(_,o),d()})},data(){return{artist:{},albums_list:new h,groupby_options:[{id:1,name:this.$t("page.artist.sort-by.name"),options:B("name_sort",!0)},{id:2,name:this.$t("page.artist.sort-by.release-date"),options:L("date_released",{direction:"asc"})}],show_details_modal:!1}},computed:{albums(){const t=this.groupby_options.find(s=>s.id===this.selected_groupby_option_id);return this.albums_list.group(t.options),this.albums_list},selected_groupby_option_id:{get(){return this.$store.state.artist_albums_sort},set(t){this.$store.commit(w,t)}}},methods:{open_tracks(){this.$router.push({name:"music-artist-tracks",params:{id:this.artist.id}})},play(){p.player_play_uri(this.albums.items.map(t=>t.uri).join(","),!0)}}},D={class:"columns"},M={class:"column"},N=["textContent"],P=["textContent"],T={class:"buttons is-centered"},U=["textContent"],j={class:"heading has-text-centered-mobile"},E=["textContent"],G=e("span",null," | ",-1),O=["textContent"];function V(t,s,d,_,o,n){const b=r("control-dropdown"),c=r("mdicon"),g=r("list-albums"),f=r("modal-dialog-artist"),y=r("content-with-heading");return k(),x("div",null,[i(y,null,{options:m(()=>[e("div",D,[e("div",M,[e("p",{class:"heading mb-5",textContent:l(t.$t("page.artist.sort-by.title"))},null,8,N),i(b,{value:n.selected_groupby_option_id,"onUpdate:value":s[0]||(s[0]=a=>n.selected_groupby_option_id=a),options:o.groupby_options},null,8,["value","options"])])])]),"heading-left":m(()=>[e("p",{class:"title is-4",textContent:l(o.artist.name)},null,8,P)]),"heading-right":m(()=>[e("div",T,[e("a",{class:"button is-small is-light is-rounded",onClick:s[1]||(s[1]=a=>o.show_details_modal=!0)},[i(c,{class:"icon",name:"dots-horizontal",size:"16"})]),e("a",{class:"button is-small is-dark is-rounded",onClick:s[2]||(s[2]=(...a)=>n.play&&n.play(...a))},[i(c,{class:"icon",name:"shuffle",size:"16"}),e("span",{textContent:l(t.$t("page.artist.shuffle"))},null,8,U)])])]),content:m(()=>[e("p",j,[e("span",{textContent:l(t.$t("page.artist.album-count",{count:o.artist.album_count}))},null,8,E),G,e("a",{class:"has-text-link",onClick:s[3]||(s[3]=(...a)=>n.open_tracks&&n.open_tracks(...a)),textContent:l(t.$t("page.artist.track-count",{count:o.artist.track_count}))},null,8,O)]),i(g,{albums:n.albums,hide_group_title:!0},null,8,["albums"]),i(f,{show:o.show_details_modal,artist:o.artist,onClose:s[4]||(s[4]=a=>o.show_details_modal=!1)},null,8,["show","artist"])]),_:1})])}const Q=C(z,[["render",V]]);export{Q as default};