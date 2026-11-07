#!/usr/bin/env python3
"""Camera-relative ABC experiment. Never a calibrated panel sRGB measurement.

analyze manifest: {"control":"control.jpg", "pages":{"0":"b0.jpg", ...,
"7":"b7.jpg"}, "repeats":[{"B":4,"path":"b4-repeat.jpg"}]}
Paths are relative to the manifest. Corners are EXIF-transposed image coordinates,
TL, TR, BR, BL, in innerScreenCorners (one fixed quadrilateral for all captures).
Optional captureSettings records UI observations or DirectShow-UVC-readback;
sidecar accepts an inline object or a relative JSON path.
These evidence records never substitute for absent EXIF or verify sensor settings.
compare requires an explicit crop in EXIF-transposed source-image coordinates.
Only abc9_lab_io owns film packing; this tool does not reimplement the wire format.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import tempfile
from pathlib import Path

import cv2
import numpy as np
from PIL import Image, ImageOps
from scipy.spatial import cKDTree

SIZE = (480, 720)
MEANING = "camera-relative/uncalibrated-colorimetry"
WARNINGS = [
    "Camera RGB and approximate Lab depend on JPEG processing, lighting and viewing geometry.",
    "The sRGB transfer/matrix below is an approximation, not a camera characterization.",
    "Normalization uses one control's dark/white anchors for every page; no per-page stretching.",
    "EXIF matching does not establish fixed white balance or identical camera processing.",
    "Spatial patch spread is a noise proxy, not a sensor SNR measurement.",
    "Repeat coverage is reported; repeats do not prove long-term stability or calibrated color accuracy.",
    "Near-duplicate thresholds are diagnostic; retained counts do not prove human-distinct stable colors.",
    "Image previews are simulated in the relative camera space, not predictions of calibrated panel output.",
]


def read_json(path: Path):
    return json.loads(path.read_text(encoding="utf-8-sig"))


def write_json(path: Path, value):
    path.write_text(json.dumps(value, indent=2, ensure_ascii=False, allow_nan=False) + "\n", encoding="utf-8")


def linear_rgb(rgb):
    rgb = np.asarray(rgb, dtype=np.float64) / 255
    return np.where(rgb <= .04045, rgb / 12.92, ((rgb + .055) / 1.055) ** 2.4)


def encoded_rgb(linear):
    linear = np.clip(linear, 0, 1)
    return 255 * np.where(linear <= .0031308, linear * 12.92, 1.055 * linear ** (1 / 2.4) - .055)


def approximate_lab(rgb):
    # CIE Lab D65 using the nominal sRGB matrix. Relative-camera approximation only.
    xyz = linear_rgb(rgb) @ np.array([[.4124564, .3575761, .1804375],
                                    [.2126729, .7151522, .0721750],
                                    [.0193339, .1191920, .9503041]]).T
    xyz /= np.array([.95047, 1, 1.08883])
    delta = 6 / 29
    f = np.where(xyz > delta ** 3, np.cbrt(xyz), xyz / (3 * delta ** 2) + 4 / 29)
    return np.stack([116 * f[..., 1] - 16, 500 * (f[..., 0] - f[..., 1]),
                     200 * (f[..., 1] - f[..., 2])], axis=-1)


def exif_metadata(image):
    root = image.getexif()
    exif = root.get_ifd(34665)
    def value(tag):
        item = exif.get(tag, root.get(tag))
        if item is None:
            return None
        if isinstance(item, (int, float)):
            return item
        try:
            return float(item)
        except (TypeError, ValueError):
            return str(item)
    return {"exposureSeconds": value(33434), "ISO": value(34855),
            "whiteBalance": value(41987), "exposureMode": value(41986),
            "fNumber": value(33437), "exifOrientation": root.get(274, 1),
            "whiteBalanceMeaning": {0: "auto", 1: "manual"}.get(value(41987), "unknown")}


def sample_photo(path: Path, quad):
    with Image.open(path) as original:
        metadata = exif_metadata(original)
        rgb = np.array(ImageOps.exif_transpose(original).convert("RGB"))
    corners = np.asarray(quad, dtype=np.float32)
    if corners.shape != (4, 2) or not np.isfinite(corners).all():
        raise ValueError("innerScreenCorners must contain four finite x/y pairs")
    if (corners < 0).any() or (corners[:, 0] >= rgb.shape[1]).any() or (corners[:, 1] >= rgb.shape[0]).any():
        raise ValueError(f"Corners outside EXIF-transposed photograph: {path}")
    if not cv2.isContourConvex(corners) or abs(cv2.contourArea(corners)) < 100:
        raise ValueError("Corners must be a nondegenerate convex TL/TR/BR/BL quadrilateral")
    matrix = cv2.getPerspectiveTransform(corners, np.float32([[0, 0], [479, 0], [479, 719], [0, 719]]))
    rectified = cv2.warpPerspective(rgb, matrix, SIZE, flags=cv2.INTER_LINEAR)
    samples = []
    for a in range(8):
        for c in range(8):
            x, y = 12 + c * 57, 105 + a * 70
            patch = rectified[y + 33:y + 39, x + 23:x + 31].reshape(-1, 3).astype(float)
            median = np.median(patch, axis=0)
            mad = 1.4826 * np.median(abs(patch - median), axis=0)
            samples.append({"A": a, "C": c, "cameraRGB": median.tolist(),
                            "rawCameraApproxLab": approximate_lab(median).tolist(),
                            "spatialMAD": mad.tolist(),
                            "highlightClippedFraction": float(np.mean(np.any(patch >= 250, axis=1))),
                            "shadowClippedFraction": float(np.mean(np.any(patch <= 1, axis=1))),
                            "allChannelsShadowClippedFraction": float(np.mean(np.all(patch <= 1, axis=1))),
                            "allChannelsHighlightClippedFraction": float(np.mean(np.all(patch >= 250, axis=1))),
                            "signalToSpatialSpreadProxy": float(np.max(median) / max(1, np.max(mad)))})
    record = {"path": str(path.resolve()), "sha256": hashlib.sha256(path.read_bytes()).hexdigest(),
              "exif": metadata, "transposedSize": [rgb.shape[1], rgb.shape[0]], "samples": samples}
    return record, rectified


def exif_check(records, control):
    reference = records[control]["exif"]
    result = {"reference": control, "byCapture": {}}
    for name, record in records.items():
        current = record["exif"]
        mismatches, missing = [], []
        for key in ("exposureSeconds", "ISO", "whiteBalance", "fNumber", "exposureMode"):
            if current[key] is None or reference[key] is None:
                missing.append(key)
            elif current[key] != reference[key]:
                mismatches.append(key)
        result["byCapture"][name] = {"mismatches": mismatches, "missing": missing,
            "exposureISOMatch": not any(k in mismatches + missing for k in ("exposureSeconds", "ISO")),
            "fixedWhiteBalanceVerified": current["whiteBalance"] == reference["whiteBalance"] == 1}
    result["allObservedFieldsConsistent"] = not any(v["mismatches"] for v in result["byCapture"].values())
    result["allExposureISOObservedAndMatched"] = all(v["exposureISOMatch"] for v in result["byCapture"].values())
    result["fixedWhiteBalanceVerified"] = all(v["fixedWhiteBalanceVerified"] for v in result["byCapture"].values())
    return result


def greedy_representatives(entries, threshold):
    kept = []
    for entry in entries:
        lab = np.asarray(entry["approxLab"])
        if not kept or min(np.linalg.norm(lab - np.asarray(k["approxLab"])) for k in kept) >= threshold:
            kept.append(entry)
    return kept


def analyze(manifest_path, corners_path, output, repeat_delta=8., max_spread=12., white_code=15, dark_code=3):
    manifest = read_json(manifest_path)
    settings = manifest.get("captureSettings", {})
    if not isinstance(settings, dict):
        raise ValueError("captureSettings must be an object of declared capture settings")
    sidecar = manifest.get("sidecar")
    if isinstance(sidecar, str):
        sidecar_path = manifest_path.parent / sidecar
        sidecar = {"path": str(sidecar_path.resolve()),
                   "sha256": hashlib.sha256(sidecar_path.read_bytes()).hexdigest(),
                   "data": read_json(sidecar_path)}
    elif sidecar is not None and not isinstance(sidecar, dict):
        raise ValueError("sidecar must be a JSON path or inline object")
    input_source = settings.get("source", "photograph-EXIF-inspected")
    if set(manifest["pages"]) != {str(i) for i in range(8)}:
        raise ValueError("manifest pages must contain exactly B keys 0..7")
    quad = read_json(corners_path)["innerScreenCorners"]
    files = {"control": manifest["control"], **{f"b{b}": manifest["pages"][str(b)] for b in range(8)}}
    repeats = manifest.get("repeats", [])
    for i, repeat in enumerate(repeats):
        if not isinstance(repeat["B"], int) or not 0 <= repeat["B"] <= 7:
            raise ValueError("repeat B must be an integer 0..7")
        files[f"repeat{i}-b{repeat['B']}"] = repeat["path"]
    records = {}
    rectified_images = {}
    for name, filename in files.items():
        records[name], rectified_images[name] = sample_photo(manifest_path.parent / filename, quad)
    consistency = exif_check(records, "control")
    control = records["control"]["samples"]
    if not (0 <= dark_code < 64 and 0 <= white_code < 64 and dark_code != white_code):
        raise ValueError("Control reference codes must be distinct indexes 0..63")
    dark = linear_rgb(control[dark_code]["cameraRGB"])
    white = linear_rgb(control[white_code]["cameraRGB"])
    span = white - dark
    if np.min(span) < .015:
        raise ValueError("Control white/dark reference has insufficient per-channel range; inspect anchors/corners")
    for record in records.values():
        for sample in record["samples"]:
            normalized = (linear_rgb(sample["cameraRGB"]) - dark) / span
            rgb = encoded_rgb(normalized)
            sample.update({"normalizedLinearRGBUnclipped": normalized.tolist(),
                           "normalizedRGB": rgb.tolist(), "approxLab": approximate_lab(rgb).tolist()})
    for name, record in records.items():
        for index, sample in enumerate(record["samples"]):
            reasons, warnings = [], []
            dark_anchor = name == "control" and index == dark_code
            white_anchor = name == "control" and index == white_code
            if sample["highlightClippedFraction"] > .05:
                warnings.append("one-or-more-channels-highlight-clipped")
            if sample["shadowClippedFraction"] > .25:
                warnings.append("one-or-more-channels-shadow-clipped")
            if sample["allChannelsHighlightClippedFraction"] > .05:
                if white_anchor:
                    warnings.append("censored-white-baseline-cannot-resolve-highlights")
                else:
                    reasons.append("all-channels-highlight-clipped")
            if sample["allChannelsShadowClippedFraction"] > .25:
                if dark_anchor:
                    warnings.append("censored-dark-baseline-cannot-resolve-dark-colors")
                else:
                    reasons.append("all-channels-shadow-clipped")
            if sample["signalToSpatialSpreadProxy"] < 3 or max(sample["cameraRGB"]) < 3:
                if dark_anchor:
                    warnings.append("dark-baseline-low-signal")
                else:
                    reasons.append("low-signal-to-spatial-spread-proxy")
            if max(sample["spatialMAD"]) > max_spread:
                reasons.append("high-spatial-spread")
            if consistency["byCapture"][name]["mismatches"]:
                reasons.append("capture-exif-mismatch")
            if record["transposedSize"] != records["control"]["transposedSize"]:
                reasons.append("capture-dimensions-mismatch")
            sample.update({"exclusionReasons": reasons, "qualityWarnings": warnings, "repeatDifferences": []})
    for i, repeat in enumerate(repeats):
        name = f"repeat{i}-b{repeat['B']}"
        base = records[f"b{repeat['B']}"]["samples"]
        for first, second in zip(base, records[name]["samples"]):
            difference = {"capture": name,
                "cameraRGBDelta": (np.asarray(second["cameraRGB"]) - first["cameraRGB"]).tolist(),
                "rawCameraApproxDeltaE76": float(np.linalg.norm(np.asarray(first["rawCameraApproxLab"]) - second["rawCameraApproxLab"])),
                "approxDeltaE76Meaning": "render-normalized clipped preview space; censored zeros do not prove equal camera or panel response",
                "approxDeltaE76": float(np.linalg.norm(np.asarray(first["approxLab"]) - second["approxLab"]))}
            first["repeatDifferences"].append(difference)
            if difference["approxDeltaE76"] > repeat_delta:
                first["exclusionReasons"].append("repeat-inconsistent")
            if second["exclusionReasons"]:
                first["exclusionReasons"].append("repeat-quality-unreliable")
    abc_entries, control_entries = [], []
    for name in ["control"] + [f"b{b}" for b in range(8)]:
        for sample in records[name]["samples"]:
            a, c = sample["A"], sample["C"]
            b = a if name == "control" else int(name[1:])
            entry = dict(sample, B=b, code=(a << 6) | (b << 3) | c,
                         controlIndex=(a << 3) | c if name == "control" else None,
                         label=f"A{a}B{b}C{c}", capture=name,
                         candidateStatus="excluded" if sample["exclusionReasons"] else "provisional-camera-relative",
                         repeatEvidence="one-or-more-repeats" if sample["repeatDifferences"] else "not-repeated")
            (control_entries if name == "control" else abc_entries).append(entry)
    candidates = [entry for entry in abc_entries if not entry["exclusionReasons"]]
    raw_entries = [dict(entry, approxLab=entry["rawCameraApproxLab"]) for entry in abc_entries]
    raw_repeats_by_b = {}
    for b in range(8):
        values = [difference["rawCameraApproxDeltaE76"] for entry in abc_entries if entry["B"] == b
                  for difference in entry["repeatDifferences"]]
        raw_repeats_by_b[str(b)] = {"sampleComparisons": len(values),
            **(dict(zip(("median", "p90", "max"), np.percentile(values, [50, 90, 100]).tolist())) if values else
               {"median": None, "p90": None, "max": None})}
    result = {"schemaVersion": 1, "kind": "experimental-abc-camera-palette", "colorMeaning": MEANING,
        "colorimetricCalibration": False, "claims512StableColors": False, "notes": WARNINGS,
        "manifest": str(manifest_path.resolve()), "corners": quad,
        "captureEvidence": {"source": input_source, "declaredCaptureSettings": settings,
            "sidecar": sidecar, "sensorParametersIndependentlyVerified": False,
            "settingsMeaning": (
                "Standard driver control Get/Set readback; not independent verification of actual sensor settings or colorimetric calibration."
                if input_source == "DirectShow-UVC-readback" else
                "User/UI observation only; not independent verification of actual sensor settings."),
            "missingExifPolicy": "Keep provisional camera-relative candidates; report missing metadata explicitly, never treat as verified exposure/WB.",
            "videoFrameLimitation": "For UI-verified-video: frames may be median/averaged from one manually fixed continuous stream. Video processing, encoding, frame selection and absence of sensor EXIF limit inference; RGB is not calibrated panel reflectance/colorimetry."},
        "sampleRectWithinCell": [23, 33, 31, 39], "exifConsistency": consistency,
        "normalization": {"method": "shared-control-linear-two-point-per-channel",
            "whiteControlIndex": white_code, "darkControlIndex": dark_code,
            "whiteCameraRGB": control[white_code]["cameraRGB"], "darkCameraRGB": control[dark_code]["cameraRGB"],
            "whiteLinearRGB": white.tolist(), "darkLinearRGB": dark.tolist(),
            "outOfRangePolicy": "preserve unclipped values in records; clip only approximate Lab/preview"},
        "thresholds": {"repeatApproxDeltaE76": repeat_delta, "maxSpatialMADCameraByte": max_spread,
            "maxHighlightClippedFraction": .05, "maxShadowClippedFraction": .25,
            "clippingPolicy": "Single-channel clipping is diagnostic only; all-channel clipping excludes candidates except the control dark/white baseline anchors. Censored baselines do not resolve clipped dark/highlight differences or establish accurate colorimetry.",
            "minSignalToSpatialSpreadProxy": 3, "meaning": "heuristic quality filters, not perceptual/colorimetric acceptance"},
        "captures": {name: {**{k: v for k, v in record.items() if k != "samples"},
                            "inputSource": input_source} for name, record in records.items()},
        "control": control_entries, "entries": abc_entries, "usableCandidateCodes": [e["code"] for e in candidates],
        "usableCandidateCodesMeaning": "Render-normalized preview space quality/repeat screening; not a physical distinguishable-color upper limit.",
        "repeatSamples": {name: r["samples"] for name, r in records.items() if name.startswith("repeat")},
        "repeatCoverage": {"B": sorted({repeat["B"] for repeat in repeats}),
            "countByB": {str(b): sum(repeat["B"] == b for repeat in repeats) for b in range(8)},
            "longTermStabilityVerified": False},
        "measurementDiagnostics": {
            "meaning": "All 512 cameraRGB samples before render normalization/clipping, using nominal sRGB approximate Lab; not calibrated colorimetry or a stable-color count.",
            "rawCameraRepeatApproxDeltaE76ByB": raw_repeats_by_b,
            "rawCameraNearDuplicateDiagnostics": [{"approxDeltaE76Threshold": threshold,
                "retainedCount": len(greedy_representatives(raw_entries, threshold)),
                "method": "all 512, greedy in B/A/C order; order dependent, not a stable/distinct-color count"}
                for threshold in (1, 2, 3, 5, 8)],
            "normalizedClipping": {"sampleCount": len(abc_entries),
                "anyChannelBelowBlackAnchorCount": sum(any(v < 0 for v in e["normalizedLinearRGBUnclipped"]) for e in abc_entries),
                "fullyClippedBlackCount": sum(all(v == 0 for v in e["normalizedRGB"]) for e in abc_entries)}},
        "nearDuplicateDiagnostics": [{"approxDeltaE76Threshold": threshold,
            "retainedCount": len(greedy_representatives(candidates, threshold)),
            "space": "render-normalized clipped preview; filtered candidates, not a physical color upper limit",
            "method": "greedy in B/A/C order; order dependent, not distinct-color proof"} for threshold in (1, 2, 3, 5, 8)]}
    output.mkdir(parents=True, exist_ok=True)
    for name, rgb in rectified_images.items():
        Image.fromarray(rgb).save(output / f"{name}-rectified.png")
    write_json(output / "experimental-palette.json", result)
    return result


def nearest_quantize(rgb, entries, index_key):
    if not entries:
        raise ValueError("No usable measured candidates; inspect experimental-palette.json")
    labs = np.array([entry["approxLab"] for entry in entries])
    _, indexes = cKDTree(labs).query(approximate_lab(rgb).reshape(-1, 3))
    chosen = np.array([entry[index_key] for entry in entries], dtype=np.uint16)[indexes].reshape(rgb.shape[:2])
    preview = np.array([entry["normalizedRGB"] for entry in entries])[indexes].reshape(rgb.shape)
    return chosen, np.rint(preview).clip(0, 255).astype(np.uint8)


def compare(palette_path, image_path, crop, output, diagnostic_dedupe=0.):
    palette = read_json(palette_path)
    if palette["colorMeaning"] != MEANING:
        raise ValueError("Expected camera-relative experiment palette")
    with Image.open(image_path) as original:
        image = ImageOps.exif_transpose(original).convert("RGB")
        x0, y0, x1, y1 = crop
        if not (0 <= x0 < x1 <= image.width and 0 <= y0 < y1 <= image.height):
            raise ValueError("Crop must be inside EXIF-transposed source")
        image = image.crop(crop).resize(SIZE, Image.Resampling.LANCZOS)
    source = np.asarray(image)
    # Source content is a nominal sRGB target, not a panel reflectance capture.
    # Only measured panel candidates use shared-control normalization. Their
    # relative camera space is an approximation, not absolute colorimetry.
    relative_source = source
    control = [entry for entry in palette["control"] if not entry["exclusionReasons"]]
    experimental = [entry for entry in palette["entries"] if entry["code"] in palette["usableCandidateCodes"]]
    if diagnostic_dedupe > 0:
        experimental = greedy_representatives(experimental, diagnostic_dedupe)
    control_indexes, control_preview = nearest_quantize(relative_source, control, "controlIndex")
    abc_codes, abc_preview = nearest_quantize(relative_source, experimental, "code")
    # Format2 diagnostic firmware interprets native body[0] as a mode marker.
    # This single portrait pixel selects mode1: rewrite A in phase2, matching
    # ABC9's RAM transfer count. Original ColorQual mode0 reuses A RAM and
    # therefore has different phase2 timing despite the same A,A,C codes.
    control_indexes[719, 0] = 8
    control_preview[719, 0] = np.rint(palette["control"][8]["normalizedRGB"]).clip(0, 255).astype(np.uint8)
    # Import independently owned protocol code before writing comparison files.
    from abc9_lab_io import pack_abc9_film, pack_qual_film
    control_film = pack_qual_film(Image.fromarray(control_indexes.astype(np.int32)))
    abc_film = pack_abc9_film(Image.fromarray(abc_codes.astype(np.int32)))
    output.mkdir(parents=True, exist_ok=True)
    image.save(output / "source-crop-480x720.png")
    Image.fromarray(np.rint(relative_source).clip(0, 255).astype(np.uint8)).save(output / "source-target-srgb.png")
    np.save(output / "control-portrait-indexes.npy", control_indexes.astype(np.uint8))
    np.save(output / "experimental-portrait-abc9.npy", abc_codes)
    Image.fromarray(control_preview).save(output / "control-simulated.png")
    Image.fromarray(abc_preview).save(output / "experimental-simulated.png")
    (output / "control.film").write_bytes(control_film)
    (output / "experimental-abc9.film").write_bytes(abc_film)
    report = {"colorMeaning": MEANING, "notes": WARNINGS,
        "source": str(image_path.resolve()), "sourceSHA256": hashlib.sha256(image_path.read_bytes()).hexdigest(),
        "crop": list(crop), "sourceInterpretation": "nominal sRGB target content; no panel dark/white normalization applied",
        "sourceLimitation": "Same nominal sRGB target for both outputs; panel candidates remain shared-control-normalized camera-relative approximations. Comparing these spaces cannot establish absolute color matching or calibrated panel output.",
        "resize": "Pillow LANCZOS, one shared crop, 480x720", "quantization": "approximate Lab Euclidean nearest, no dithering",
        "ABC9Encoding": "(A<<6)|(B<<3)|C in uint16 portrait array", "controlEncoding": "(A<<3)|C, 0..63",
        "controlDiagnosticMarker": {"portraitPixel": [0, 719], "index": 8, "mode": 1,
                                    "meaning": "A,A,C rewrite-A control; phase2 RAM transfer matches ABC9; one metadata pixel",
                                    "originalColorQualDifference": "Original mode0 reuses A RAM in phase2; this mode1 rewrites A for a matched transfer count"},
        "experimentalFormat": "private 0x7F, CONFIG_ARK_EPD_ABC_DIAG firmware only; not a production feature",
        "controlCandidates": len(control), "experimentalCandidates": len(experimental),
        "diagnosticDedupeThreshold": diagnostic_dedupe,
        "controlUsedIndexes": np.unique(control_indexes).tolist(), "experimentalUsedCodes": np.unique(abc_codes).tolist(),
        "palette": str(palette_path.resolve())}
    write_json(output / "comparison.json", report)
    return report


def self_test():
    """One synthetic contract check; never counts as physical color validation."""
    with tempfile.TemporaryDirectory(prefix="camera-palette-contract-") as temporary:
        root = Path(temporary)
        for b in range(8):
            canvas = np.full((720, 480, 3), 200, dtype=np.uint8)
            for a in range(8):
                for c in range(8):
                    x, y = 12 + c * 57, 105 + a * 70
                    canvas[y + 33:y + 39, x + 23:x + 31] = [30 + a * 18, 40 + b * 16, 35 + c * 19]
            if b == 0:
                canvas[138:144, 206:214] = [0, 0, 0]  # censored control dark anchor index3
                canvas[208:214, 434:442] = [255, 255, 255]  # censored control white anchor index15
            if b == 1:
                canvas[138:144, 35:43] = [0, 100, 150]  # single-channel clipping
                canvas[138:144, 92:100] = [0, 0, 0]  # all-channel clipping
                canvas[138:144, 149:157] = [255, 100, 150]  # one highlight channel
            Image.fromarray(canvas).save(root / f"b{b}.png")
        write_json(root / "manifest.json", {"control": "b0.png", "pages": {str(b): f"b{b}.png" for b in range(8)},
                                               "captureSettings": {"source": "DirectShow-UVC-readback", "ISO": 1250},
                                               "sidecar": {"continuousStream": True},
                                               "repeats": [{"B": 2, "path": "b2.png"}]})
        write_json(root / "corners.json", {"innerScreenCorners": [[0, 0], [479, 0], [479, 719], [0, 719]]})
        result = analyze(root / "manifest.json", root / "corners.json", root / "result")
        assert len(result["entries"]) == 512 and len(result["control"]) == 64
        assert result["exifConsistency"]["fixedWhiteBalanceVerified"] is False
        assert result["exifConsistency"]["allExposureISOObservedAndMatched"] is False
        assert result["captureEvidence"]["source"] == "DirectShow-UVC-readback"
        assert result["captureEvidence"]["settingsMeaning"].startswith("Standard driver control Get/Set readback;")
        assert result["captureEvidence"]["sensorParametersIndependentlyVerified"] is False
        assert result["repeatCoverage"]["B"] == [2]
        assert result["repeatCoverage"]["countByB"] == {str(b): int(b == 2) for b in range(8)}
        assert result["repeatCoverage"]["longTermStabilityVerified"] is False
        assert result["captureEvidence"]["sidecar"]["continuousStream"] is True
        assert result["usableCandidateCodes"]
        assert not result["control"][3]["exclusionReasons"]
        assert not result["control"][15]["exclusionReasons"]
        assert "censored-dark-baseline-cannot-resolve-dark-colors" in result["control"][3]["qualityWarnings"]
        assert not result["entries"][64]["exclusionReasons"]
        assert "all-channels-shadow-clipped" in result["entries"][65]["exclusionReasons"]
        assert not result["entries"][66]["exclusionReasons"]
        assert result["entries"][128]["repeatDifferences"][0]["approxDeltaE76"] == 0
        chosen, preview = nearest_quantize(np.array([[[30, 40, 35]]]), result["entries"], "code")
        assert chosen.shape == (1, 1) and preview.shape == (1, 1, 3)
        assert len({entry["code"] for entry in result["entries"]}) == 512
        np.testing.assert_allclose(result["control"][3]["normalizedRGB"], [0, 0, 0], atol=1e-6)
        np.testing.assert_allclose(result["control"][15]["normalizedRGB"], [255] * 3, atol=1e-6)
        comparison = compare(root / "result/experimental-palette.json", root / "b0.png",
                             (0, 0, 480, 720), root / "comparison")
        assert comparison["experimentalCandidates"] > 0
        with Image.open(root / "comparison/source-target-srgb.png") as target, Image.open(root / "b0.png") as original:
            np.testing.assert_array_equal(np.asarray(target), np.asarray(original))
        assert (root / "comparison/control.film").stat().st_size == 32 + 480 * 720
        assert (root / "comparison/experimental-abc9.film").stat().st_size == 32 + 480 * 720 * 2
        packed = (root / "comparison/experimental-abc9.film").read_bytes()
        assert packed[9] == 0x7F and packed[12:16] == b"ABC9"
        assert (root / "comparison/control.film").read_bytes()[32] == 8
        # A raised control black anchor censors two different camera responses to the same preview black.
        with Image.open(root / "b0.png") as original:
            raised_control = np.array(original)
            changed_repeat = np.array(original)
        raised_control[138:144, 206:214] = [80, 80, 80]
        changed_repeat[138:144, 35:43] = [35, 45, 40]
        Image.fromarray(raised_control).save(root / "raised-control.png")
        Image.fromarray(changed_repeat).save(root / "changed-repeat.png")
        clipped_manifest = read_json(root / "manifest.json")
        clipped_manifest.update(control="raised-control.png", repeats=[{"B": 0, "path": "changed-repeat.png"}])
        write_json(root / "clipped-manifest.json", clipped_manifest)
        clipped = analyze(root / "clipped-manifest.json", root / "corners.json", root / "clipped-result")
        difference = clipped["entries"][0]["repeatDifferences"][0]
        assert difference["approxDeltaE76"] == 0
        assert difference["rawCameraApproxDeltaE76"] > 0
        assert clipped["entries"][0]["rawCameraApproxLab"] != clipped["repeatSamples"]["repeat0-b0"][0]["rawCameraApproxLab"]
        assert clipped["measurementDiagnostics"]["normalizedClipping"]["fullyClippedBlackCount"] > 0
        assert clipped["measurementDiagnostics"]["rawCameraRepeatApproxDeltaE76ByB"]["0"]["sampleComparisons"] == 64
        assert len(clipped["measurementDiagnostics"]["rawCameraNearDuplicateDiagnostics"]) == 5
    return {"syntheticContract": "pass", "physicalValidation": False}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest="command", required=True)
    analyze_parser = sub.add_parser("analyze")
    for name in ("manifest", "corners", "output"):
        analyze_parser.add_argument(f"--{name}", type=Path, required=True)
    analyze_parser.add_argument("--repeat-delta", type=float, default=8.)
    analyze_parser.add_argument("--max-spread", type=float, default=12.)
    analyze_parser.add_argument("--white-code", type=int, default=15)
    analyze_parser.add_argument("--dark-code", type=int, default=3)
    comparison_parser = sub.add_parser("compare")
    for name in ("palette", "image", "output"):
        comparison_parser.add_argument(f"--{name}", type=Path, required=True)
    comparison_parser.add_argument("--crop", type=int, nargs=4, required=True, metavar=("X0", "Y0", "X1", "Y1"))
    comparison_parser.add_argument("--diagnostic-dedupe", type=float, default=0.)
    sub.add_parser("self-test")
    args = parser.parse_args()
    if args.command == "analyze":
        result = analyze(args.manifest, args.corners, args.output, args.repeat_delta, args.max_spread, args.white_code, args.dark_code)
        print(json.dumps({"output": str(args.output), "usableCandidates": len(result["usableCandidateCodes"]),
                          "exifConsistency": result["exifConsistency"]}, indent=2))
    elif args.command == "compare":
        print(json.dumps(compare(args.palette, args.image, args.crop, args.output, args.diagnostic_dedupe), indent=2))
    else:
        print(json.dumps(self_test()))


if __name__ == "__main__":
    main()
