var test = require('node:test');
var assert = require('node:assert/strict');
var fs = require('fs');
var path = require('path');
var frame = require('../src/pkjs/frame');
var emit = require('../scripts/emit-layout');

test('layout.h matches the emitter', function() {
  var actual = fs.readFileSync(path.join(__dirname, '..', 'src', 'c', 'layout.h'), 'utf8');
  assert.equal(actual, emit.headerText());
});

test('gabbro rectangle corners stay inside radius 124 of (130, 130)', function() {
  var layout = frame.layouts.gabbro;
  var name;
  for (name in layout) {
    var rect = layout[name];
    if (typeof rect.w !== 'number' || typeof rect.h !== 'number') continue;
    var corners = [
      [rect.x, rect.y],
      [rect.x + rect.w, rect.y],
      [rect.x, rect.y + rect.h],
      [rect.x + rect.w, rect.y + rect.h]
    ];
    var i;
    for (i = 0; i < corners.length; i++) {
      var dx = corners[i][0] - 130;
      var dy = corners[i][1] - 130;
      var distance = Math.sqrt(dx * dx + dy * dy);
      assert.ok(distance <= 124, name + ' corner ' + corners[i].join(',') + ' is ' + distance);
    }
  }
});
