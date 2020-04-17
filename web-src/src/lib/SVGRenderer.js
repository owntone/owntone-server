/*
 * SVGRenderer taken from https://github.com/bendera/placeholder published under MIT License
 * Copyright (c) 2017 Adam Bender
 * https://github.com/bendera/placeholder/blob/master/LICENSE
 */
class SVGRenderer {
  render (data) {
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
}

export default SVGRenderer
