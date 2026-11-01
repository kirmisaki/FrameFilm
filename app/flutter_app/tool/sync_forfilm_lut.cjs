// Match ForFilm: carry lookup tables with code, without separate HTTP assets.
// Run: node app/flutter_app/tool/sync_forfilm_lut.cjs
const fs = require('node:fs');
const path = require('node:path');
const source = fs.readFileSync(path.resolve(__dirname, '../../../tools/ForFilm/js/atkinson_enhanced_lut.js'), 'utf8');
const tables = [];
for (const [variable, name, length] of [
  ['AE_CORRECTION_LUT_B64', 'Correction', 64 * 64 * 64 * 3],
  ['AE_SELECTION_LUT_B64', 'Selection', 64 * 64 * 64],
]) {
  const match = source.match(new RegExp('var\\s+' + variable + '\\s*=\\s*"([^"]+)"'));
  if (!match) throw new Error('Missing source table: ' + variable);
  if (Buffer.from(match[1], 'base64').length !== length) throw new Error('Unexpected table size: ' + variable);
  tables.push('const String _encoded' + name + ' =\n' + match[1].match(/.{1,100}/g).map(line => '    ' + JSON.stringify(line)).join('\n') + ';\n');
}
const dart = "// Generated from tools/ForFilm/js/atkinson_enhanced_lut.js. Do not edit.\n"
  + "import 'dart:convert';\nimport 'dart:typed_data';\n"
  + "final Uint8List forFilmCorrectionLut = base64Decode(_encodedCorrection);\n"
  + "final Uint8List forFilmSelectionLut = base64Decode(_encodedSelection);\n"
  + tables.join('\n');
fs.writeFileSync(path.resolve(__dirname, '../lib/frame_lut.dart'), dart);
console.log('frame_lut.dart generated from the ForFilm tables');