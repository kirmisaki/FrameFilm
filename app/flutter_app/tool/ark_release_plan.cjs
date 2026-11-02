#!/usr/bin/env node
const fs = require('node:fs');

const HEADING = /^## \[(\d{4}\.\d{2}\.\d{2}\.\d+)\] - (\d{4}-\d{2}-\d{2})\s*$/gm;

function sections(markdown) {
  const matches = [...markdown.matchAll(HEADING)];
  return matches.map((match, index) => ({
    id: match[1],
    date: match[2],
    body: markdown.slice(match.index, matches[index + 1]?.index ?? markdown.length).trim(),
  }));
}

function plan(current, previous, pubspec, event, validatePubspec = true) {
  const entries = sections(current);
  if (new Set(entries.map((entry) => entry.id)).size !== entries.length) {
    throw Error('Ark changelog has duplicate release IDs');
  }
  if (!entries.length) return { publish: false };
  const latest = entries[0];
  const priorIds = new Set(sections(previous).map((entry) => entry.id));
  if (event === 'push' && priorIds.has(latest.id)) return { publish: false };
  if (!['push', 'workflow_dispatch'].includes(event)) return { publish: false };

  const declared = latest.body.match(/^App:\s*(\d+\.\d+\.\d+)\+(\d+)\s*$/m);
  if (!declared) throw Error(`Ark ${latest.id} must declare App: name+code`);
  if (validatePubspec) {
    const version = pubspec.match(/^version:\s*(\d+\.\d+\.\d+)\+(\d+)\s*$/m);
    if (!version) throw Error('pubspec.yaml must contain version: name+code');
    if (declared[1] !== version[1] || declared[2] !== version[2]) {
      throw Error(`Ark ${latest.id} App ${declared[1]}+${declared[2]} differs from pubspec ${version[1]}+${version[2]}`);
    }
  }
  if (latest.id.slice(0, 10).replaceAll('.', '-') !== latest.date) {
    throw Error(`Ark ${latest.id} date does not match heading date`);
  }
  return {
    publish: true,
    tag: `ark-${latest.id}`,
    title: `Ark ${latest.id} · App ${declared[1]}+${declared[2]}`,
    versionName: declared[1],
    versionCode: declared[2],
    notes: latest.body,
  };
}

if (require.main === module) {
  const [currentPath, previousPath, pubspecPath, event, notesPath, candidateFlag] = process.argv.slice(2);
  if (!currentPath || !previousPath || !pubspecPath || !event) {
    throw Error('Usage: ark_release_plan.cjs <changelog> <previous-or-empty-file> <pubspec> <event> [notes-file]');
  }
  const result = plan(...[currentPath, previousPath, pubspecPath].map((file) => fs.readFileSync(file, 'utf8')), event, candidateFlag !== '--candidate');
  if (notesPath && result.publish) fs.writeFileSync(notesPath, `${result.notes}\n`);
  const output = process.env.GITHUB_OUTPUT;
  if (output) {
    fs.appendFileSync(output, `publish=${result.publish}\n`);
    if (result.publish) {
      for (const key of ['tag', 'title', 'versionName', 'versionCode']) {
        fs.appendFileSync(output, `${key}=${result[key]}\n`);
      }
    }
  }
  console.log(JSON.stringify({...result, notes: undefined}));
}

module.exports = {plan, sections};
