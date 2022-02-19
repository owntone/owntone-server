/*
 * SVGRenderer taken from https://github.com/bendera/placeholder published under MIT License
 * Copyright (c) 2017 Adam Bender
 * https://github.com/bendera/placeholder/blob/master/LICENSE
 */

import stringToColor from 'string-to-color'


function is_background_light (background_color) {
  // Based on https://stackoverflow.com/a/44615197
  const hex = background_color.replace(/#/, '')
  const r = parseInt(hex.substr(0, 2), 16)
  const g = parseInt(hex.substr(2, 2), 16)
  const b = parseInt(hex.substr(4, 2), 16)

  const luma = [
    0.299 * r,
    0.587 * g,
    0.114 * b
  ].reduce((a, b) => a + b) / 255

  return luma > 0.5
}

function calc_text_color (background_color) {
  return is_background_light(background_color) ? '#000000' : '#ffffff'
}

function createSVG (data) {
  const svg = '<svg width="' + data.width + '" height="' + data.height + '" xmlns="http://www.w3.org/2000/svg" viewBox="0 0 ' + data.width + ' ' + data.height + '" preserveAspectRatio="none">' +
      '<defs>' +
      '<style type="text/css">' +
      '    #holder text {' +
      '      fill: ' + data.textColor + ';' +
      '      font-family: ' + data.fontFamily + ';' +
      '      font-size: ' + data.fontSize + 'px;' +
      '      font-weight: ' + data.fontWeight + ';' +
      '    }' +
      '  </style>' +
      '</defs>' +
      '<g id="holder">' +
      '  <rect width="100%" height="100%" fill="' + data.backgroundColor + '"></rect>' +
      '  <g>' +
      '    <text text-anchor="middle" x="50%" y="50%" dy=".3em">' + data.caption + '</text>' +
      '  </g>' +
      '</g>' +
      '</svg>'

  return 'data:image/svg+xml;charset=UTF-8,' + encodeURIComponent(svg)
}

function renderSVG (caption, alt_text, params) {
  const background_color = stringToColor(alt_text)
  const text_color = calc_text_color(background_color)
  const paramsSVG = {
    width: params.width,
    height: params.height,
    textColor: text_color,
    backgroundColor: background_color,
    caption: caption,
    fontFamily: params.font_family,
    fontSize: params.font_size,
    fontWeight: params.font_weight
  }
  return createSVG(paramsSVG)
}

export { renderSVG }