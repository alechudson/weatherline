var test = require('node:test');
var assert = require('node:assert/strict');
var fs = require('fs');
var path = require('path');

function filesUnder(dir) {
  var found = [];
  fs.readdirSync(dir).forEach(function(name) {
    var full = path.join(dir, name);
    if (fs.statSync(full).isDirectory()) {
      found = found.concat(filesUnder(full));
    } else {
      found.push(full);
    }
  });
  return found;
}

test('source does not contain the fixture city or its coordinates', function() {
  var files = filesUnder(path.join(__dirname, '..', 'src'));
  assert.ok(files.length > 0);
  files.forEach(function(file) {
    var text = fs.readFileSync(file, 'utf8');
    assert.equal(text.indexOf('30.27'), -1, file);
    assert.equal(text.indexOf('-97.74'), -1, file);
    assert.equal(text.indexOf('Austin'), -1, file);
  });
});
