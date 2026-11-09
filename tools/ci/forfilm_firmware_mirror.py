"""Package the upstream Release catalog and its validated firmware for ForFilm Pages."""

import argparse
import hashlib
import json
import os
from pathlib import Path
import re
from urllib.error import HTTPError
from urllib.parse import quote
from urllib.request import Request, urlopen


REPOSITORY = 'kirmisaki/FrameFilm'
RELEASES_API = f'https://api.github.com/repos/{REPOSITORY}/releases'
ARK_CI_TAG = 'ark-ci-latest'
ARK_FILES = {
    'framefilm-ark-firmware.json',
    'frame_film_ark-n4r2.bin',
    'frame_film_ark-n4r2-flash.bin',
    'frame_film_ark-n16r8.bin',
    'frame_film_ark-n16r8-flash.bin',
}
CLASSIC_MANIFEST = 'framefilm-classic-firmware.json'
CLASSIC_IMAGE = re.compile(r'frame_film_pro_[a-z0-9_.-]+-(?:ota|flash)\.bin')
TAG_NAME = re.compile(r'[a-zA-Z0-9][a-zA-Z0-9._-]*')
SHA_DIGEST = re.compile(r'sha256:[0-9a-fA-F]{64}')


def get_release_json(suffix):
    headers = {'Accept': 'application/vnd.github+json', 'User-Agent': 'FrameFilm-ForFilm-Pages'}
    token = os.environ.get('GH_TOKEN')
    if token:
        headers['Authorization'] = f'Bearer {token}'
    request = Request(RELEASES_API + suffix, headers=headers)
    with urlopen(request, timeout=30) as response:
        return json.load(response)


def get_ci_release():
    try:
        return get_release_json('/tags/' + ARK_CI_TAG)
    except HTTPError as error:
        if error.code != 404:
            raise
        return None


def latest_formal(releases, classic):
    published = (
        release for release in releases
        if not release['draft'] and not release['prerelease']
        and release['tag_name'].startswith('frame-film-') == classic
        and release['published_at']
    )
    return max(published, key=lambda release: release['published_at'], default=None)


def download_asset(tag, asset):
    name = asset['name']
    url = f'https://github.com/{REPOSITORY}/releases/download/{quote(tag, safe="")}/{name}'
    with urlopen(url, timeout=60) as response:
        return response.read(asset['size'] + 1)


def mirror_release(output, release, classic=False):
    if release is None:
        return None

    tag = release['tag_name']
    if not TAG_NAME.fullmatch(tag) or release['draft']:
        raise ValueError(f'Unexpected release tag: {tag!r}')
    assets = {}
    for entry in release['assets']:
        name = entry['name']
        if classic:
            relevant = name == CLASSIC_MANIFEST or CLASSIC_IMAGE.fullmatch(name)
        else:
            relevant = name in ARK_FILES
        if not relevant:
            continue
        if name in assets:
            raise ValueError(f'{tag} has duplicate asset {name}')
        assets[name] = {key: entry[key] for key in ('id', 'name', 'size', 'digest')}

    required = {CLASSIC_MANIFEST} if classic else ARK_FILES
    if required - assets.keys() or (classic and not any(name.endswith('-flash.bin') for name in assets)):
        raise ValueError(f'{tag} does not have a complete firmware release')

    directory = output / tag
    directory.mkdir(parents=True, exist_ok=True)
    for asset in assets.values():
        if classic and asset['name'].endswith('-ota.bin'):
            continue  # The Classic manifest refers to OTA assets, but ForFilm only writes complete images.
        digest = asset['digest']
        if not digest or not SHA_DIGEST.fullmatch(digest) or not 0 < asset['size'] <= 16 * 1024 * 1024:
            raise ValueError(f'{tag}/{asset["name"]} lacks a valid size or digest')
        data = download_asset(tag, asset)
        if len(data) != asset['size'] or 'sha256:' + hashlib.sha256(data).hexdigest() != digest.lower():
            raise ValueError(f'{tag}/{asset["name"]} failed size or SHA-256 verification')
        (directory / asset['name']).write_bytes(data)

    catalog = {key: release[key] for key in ('tag_name', 'published_at', 'draft', 'prerelease')}
    catalog['body'] = release.get('body') or ''
    catalog['assets'] = list(assets.values())
    print(f'Mirrored {tag}', flush=True)
    return catalog


def build_mirror(output):
    releases = get_release_json('?per_page=100')
    catalog = {
        'arkFormal': mirror_release(output, latest_formal(releases, classic=False)),
        'arkCi': mirror_release(output, get_ci_release()),
        'classicFormal': mirror_release(output, latest_formal(releases, classic=True), classic=True),
    }
    output.mkdir(parents=True, exist_ok=True)
    (output / 'catalog.json').write_text(json.dumps(catalog, ensure_ascii=False) + '\n', encoding='utf-8')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('output', type=Path)
    build_mirror(parser.parse_args().output)
