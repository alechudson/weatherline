var fs = require('fs');
var path = require('path');
var frame = require('../src/pkjs/frame');

var RECTS = ['steps', 'battery', 'time', 'temp', 'date', 'place', 'meta', 'cloud', 'plot', 'wind', 'precip'];

function rectText(layout, name) {
  var rect = layout[name] || {x: 0, y: 0, w: 0, h: 0};
  return '  .' + name + ' = {' + rect.x + ', ' + rect.y + ', ' + rect.w + ', ' + rect.h + '},';
}

function layoutText(layout) {
  var lines = [];
  var i;
  for (i = 0; i < RECTS.length; i++) lines.push(rectText(layout, RECTS[i]));
  var bt = layout.bluetooth;
  lines.push('  .bt_x = ' + bt.x + ',');
  lines.push('  .bt_y = ' + bt.y + ',');
  lines.push('  .bt_r = ' + bt.r + ',');
  return lines.join('\n');
}

function headerText() {
  return '#pragma once\n'
    + '\n'
    + '#include <stdint.h>\n'
    + '\n'
    + 'typedef struct {\n'
    + '  int16_t x;\n'
    + '  int16_t y;\n'
    + '  int16_t w;\n'
    + '  int16_t h;\n'
    + '} Rect;\n'
    + '\n'
    + 'typedef struct {\n'
    + '  Rect steps;\n'
    + '  Rect battery;\n'
    + '  Rect time;\n'
    + '  Rect temp;\n'
    + '  Rect date;\n'
    + '  Rect place;\n'
    + '  Rect meta;\n'
    + '  Rect cloud;\n'
    + '  Rect plot;\n'
    + '  Rect wind;\n'
    + '  Rect precip;\n'
    + '  int16_t bt_x;\n'
    + '  int16_t bt_y;\n'
    + '  int16_t bt_r;\n'
    + '} Layout;\n'
    + '\n'
    + '#if defined(PBL_ROUND)\n'
    + 'static const Layout LAYOUT = {\n'
    + layoutText(frame.layouts.gabbro) + '\n'
    + '};\n'
    + '#else\n'
    + 'static const Layout LAYOUT = {\n'
    + layoutText(frame.layouts.emery) + '\n'
    + '};\n'
    + '#endif\n';
}

function writeHeader() {
  var dest = path.join(__dirname, '..', 'src', 'c', 'layout.h');
  fs.writeFileSync(dest, headerText());
}

if (require.main === module) writeHeader();

module.exports = {
  headerText: headerText,
  writeHeader: writeHeader
};
