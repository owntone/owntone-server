import{_,e as r,r as m,o as c,c as u,d as l,w as C,a as t,t as e,k as h,T as p}from"./index.js";const x={name:"ModalDialogArtist",props:["show","artist"],emits:["close"],methods:{play(){this.$emit("close"),r.player_play_uri(this.artist.uri,!1)},queue_add(){this.$emit("close"),r.queue_add(this.artist.uri)},queue_add_next(){this.$emit("close"),r.queue_add_next(this.artist.uri)},open_artist(){this.$emit("close"),this.$router.push({name:"music-artist",params:{id:this.artist.id}})}}},k={key:0,class:"modal is-active"},g={class:"modal-content fd-modal-card"},f={class:"card"},y={class:"card-content"},v={class:"title is-4"},q=["textContent"],$={class:"content is-small"},b=["textContent"],z=["textContent"],w=["textContent"],B=["textContent"],D=["textContent"],M=["textContent"],N=["textContent"],V=["textContent"],A={class:"card-footer"},T=["textContent"],E=["textContent"],S=["textContent"];function j(a,s,o,F,G,i){const d=m("mdicon");return c(),u("div",null,[l(p,{name:"fade"},{default:C(()=>[o.show?(c(),u("div",k,[t("div",{class:"modal-background",onClick:s[0]||(s[0]=n=>a.$emit("close"))}),t("div",g,[t("div",f,[t("div",y,[t("p",v,[t("a",{class:"has-text-link",onClick:s[1]||(s[1]=(...n)=>i.open_artist&&i.open_artist(...n)),textContent:e(o.artist.name)},null,8,q)]),t("div",$,[t("p",null,[t("span",{class:"heading",textContent:e(a.$t("dialog.artist.albums"))},null,8,b),t("span",{class:"title is-6",textContent:e(o.artist.album_count)},null,8,z)]),t("p",null,[t("span",{class:"heading",textContent:e(a.$t("dialog.artist.tracks"))},null,8,w),t("span",{class:"title is-6",textContent:e(o.artist.track_count)},null,8,B)]),t("p",null,[t("span",{class:"heading",textContent:e(a.$t("dialog.artist.type"))},null,8,D),t("span",{class:"title is-6",textContent:e(a.$t("data.kind."+o.artist.data_kind))},null,8,M)]),t("p",null,[t("span",{class:"heading",textContent:e(a.$t("dialog.artist.added-on"))},null,8,N),t("span",{class:"title is-6",textContent:e(a.$filters.datetime(o.artist.time_added))},null,8,V)])])]),t("footer",A,[t("a",{class:"card-footer-item has-text-dark",onClick:s[2]||(s[2]=(...n)=>i.queue_add&&i.queue_add(...n))},[l(d,{class:"icon",name:"playlist-plus",size:"16"}),t("span",{class:"is-size-7",textContent:e(a.$t("dialog.artist.add"))},null,8,T)]),t("a",{class:"card-footer-item has-text-dark",onClick:s[3]||(s[3]=(...n)=>i.queue_add_next&&i.queue_add_next(...n))},[l(d,{class:"icon",name:"playlist-play",size:"16"}),t("span",{class:"is-size-7",textContent:e(a.$t("dialog.artist.add-next"))},null,8,E)]),t("a",{class:"card-footer-item has-text-dark",onClick:s[4]||(s[4]=(...n)=>i.play&&i.play(...n))},[l(d,{class:"icon",name:"play",size:"16"}),t("span",{class:"is-size-7",textContent:e(a.$t("dialog.artist.play"))},null,8,S)])])])]),t("button",{class:"modal-close is-large","aria-label":"close",onClick:s[5]||(s[5]=n=>a.$emit("close"))})])):h("",!0)]),_:1})])}const I=_(x,[["render",j]]);export{I as M};