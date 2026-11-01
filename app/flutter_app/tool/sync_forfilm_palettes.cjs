// Run from repository root; keep measured palette and valid-index data upstream.
const fs = require('fs');
const source = fs.readFileSync('tools/ForFilm/js/convert.js', 'utf8');
let output = '// Generated from tools/ForFilm/js/convert.js, measured HEX palettes.\n';
for (const [upstream, dart] of [
  ['CF_PALETTE_FAST', 'colorFastPalette'],
  ['CF_PALETTE_QUAL', 'colorQualPalette'],
  ['CF_PALETTE_QUAL_MEASURED', 'colorQualMeasuredPalette'],
  ['CF_PALETTE_QUAL_CALIBRATED', 'colorQualCalibratedPalette'],
]) {
  const match = source.match(new RegExp('var ' + upstream + ' = (\\[[\\s\\S]*?\\n\\]);'));
  if (!match) throw new Error('Missing upstream palette: ' + upstream);
  output += 'const ' + dart + ' = <List<int>>' + match[1] + ';\n';
}
const fastCandidates = source.match(/candidateIndexes: (\[[\s\S]*?\])/);
const qualCandidates = source.match(/var CF_QUAL_CANDIDATE_INDEXES = (\[[\s\S]*?\]);/);
if (!fastCandidates || !qualCandidates) throw new Error('Missing upstream candidate indices');
output += 'const colorFastCandidates = <int>' + fastCandidates[1] + ';\n';
output += 'const colorQualCandidates = <int>' + qualCandidates[1] + ';\n';
fs.writeFileSync('app/flutter_app/lib/frame_palettes.dart', output);
