const toColor = (string) => {
  let hash = 0
  for (const char of string) {
    hash = char.charCodeAt(0) + hash * 0x1f
  }
  return (hash % 0xffffff).toString(16).padStart(6, '0')
}

const luminance = (color) =>
  [0.2126, 0.7152, 0.0722].reduce(
    (value, factor, index) =>
      value + Number(`0x${color.slice(index * 2, index * 2 + 2)}`) * factor,
    0
  ) / 255

export const renderSVG = (data) => {
  const background = toColor(data.alternate)
  let text = '#FFFFFF'
  if (luminance(background) > 0.5) {
    text = '#000000'
  }
  const svg = `<svg xmlns="http://www.w3.org/2000/svg"
    width="${data.size}" height="${data.size}"
    viewBox="0 0 ${data.size} ${data.size}">
    <rect width="100%" height="100%" fill="#${background}"/>
    <text x="50%" y="50%" dominant-baseline="middle" text-anchor="middle"
      font-weight="${data.font.weight}" font-family="${data.font.family}"
      font-size="${data.size / 3}" fill="${text}">
      ${data.caption}
    </text>
  </svg>`
  return `data:image/svg+xml;charset=UTF-8,${encodeURIComponent(svg)}`
}
