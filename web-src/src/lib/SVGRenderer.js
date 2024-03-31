const toColor = (string) => {
  let hash = 0
  for (let i = 0; i < string.length; i++) {
    hash = string.charCodeAt(i) + ((hash << 5) - hash)
  }
  return (hash & 0x00ffffff).toString(16)
}

const luminance = (color) =>
  [0.2126, 0.7152, 0.0722].reduce(
    (luminance, factor, index) =>
      luminance + Number(`0x${color.slice(index * 2, index * 2 + 2)}`) * factor,
    0
  ) / 255

export const renderSVG = (data) => {
  const color = toColor(data.alternate),
    svg = `<svg xmlns="http://www.w3.org/2000/svg"
    width="${data.size}" height="${data.size}"
    viewBox="0 0 ${data.size} ${data.size}">
    <rect width="100%" height="100%" fill="#${color}"/>
    <text x="50%" y="50%" dominant-baseline="middle" text-anchor="middle"
      font-weight="${data.font.weight}" font-family="${data.font.family}"
      font-size="${data.size / 3}" fill="${luminance(color) > 0.5 ? '#000000' : '#FFFFFF'}">
      ${data.caption}
    </text>
  </svg>`
  return `data:image/svg+xml;charset=UTF-8,${encodeURIComponent(svg)}`
}
