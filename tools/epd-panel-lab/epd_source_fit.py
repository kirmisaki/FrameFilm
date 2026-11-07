"""Source-only display-range fitting before joint ABC quantization.

CIE L* is derived from nominal sRGB Y, not characterized camera colorimetry.
Measured palette coordinates are never modified. Warm masking is a color
heuristic, not face detection. No percentile tail clipping is performed.
CIE piecewise lightness follows the project's nominal sRGB/Lab conversions.
"""
import numpy as np
from abc512_render import linear as _linear

Y_WEIGHTS = np.array([.2126, .7152, .0722])


def _lightness(y):
    delta = 6/29
    return 116*np.where(y > delta**3, np.cbrt(y), y/(3*delta**2)+4/29)-16


def _inverse_lightness(lightness):
    f = (lightness+16)/116
    delta = 6/29
    return np.where(f > delta, f**3, 3*delta**2*(f-4/29))


def _smoothstep(low, high, value):
    t = np.clip((value-low)/(high-low), 0., 1.)
    return t*t*(3-2*t)


def _fit_chroma(y, chroma):
    """Shorten chroma only as needed to fit 0..1, preserving nominal Y."""
    capacity = np.where(chroma >= 0, 1-y[...,None], y[...,None])
    ratio = np.full_like(chroma, np.inf)
    np.divide(capacity, np.abs(chroma), out=ratio, where=np.abs(chroma)>1e-15)
    scale = np.minimum(1., np.min(ratio, axis=-1))
    return y[...,None]+chroma*scale[...,None], scale


def fit_source(source_srgb, colors_linear, floor, strength=0., warm_strength=0.):
    """Return HxWx3 linear target and JSON-compatible diagnostics.

    source_srgb is numeric RGB in 0..255. colors_linear is an Nx3 measured
    palette in the existing shared-white space; used for diagnostics only.
    floor is the existing ABC minimum observed luminance, white remains 1.
    strength blends the previous linear baseline with fixed-range CIE-L*
    fitting. Both controls default to zero for exact baseline compatibility.
    """
    source = np.asarray(source_srgb)
    colors = np.asarray(colors_linear, dtype=float)
    if source.ndim != 3 or source.shape[-1] != 3 or source.size == 0:
        raise ValueError('source_srgb must be a nonempty HxWx3 image')
    if not np.isfinite(source).all() or np.any(source < 0) or np.any(source > 255):
        raise ValueError('source_srgb must be finite in 0..255')
    if colors.ndim != 2 or colors.shape[-1] != 3 or len(colors) == 0 or not np.isfinite(colors).all():
        raise ValueError('colors_linear must be a nonempty finite Nx3 palette')
    if not np.isfinite([floor,strength,warm_strength]).all() or not 0 <= floor < 1:
        raise ValueError('floor must be finite in 0..1 (excluding 1)')
    if not 0 <= strength <= 1 or not 0 <= warm_strength <= 1:
        raise ValueError('strength and warm_strength must be in 0..1')
    rgb = _linear(source)
    baseline = floor+(1-floor)*rgb
    target = baseline.copy()
    gamut_scale = np.ones(source.shape[:2])
    if strength > 0:
        source_y = rgb @ Y_WEIGHTS
        black_l = float(_lightness(floor))
        mapped_l = black_l + (100-black_l)*_lightness(source_y)/100
        mapped_y = _inverse_lightness(mapped_l)
        fitted, gamut_scale = _fit_chroma(mapped_y,(rgb-source_y[...,None])*(1-floor))
        target = baseline*(1-strength)+fitted*strength
    before_warm_y = target @ Y_WEIGHTS
    # Warm hue proxy requires positive red/yellow excess over blue. Smooth
    # gates suppress near-neutral pixels, dark pixels, and cool/blue hues.
    r,g,b = np.moveaxis(target,-1,0)
    warm_excess = np.minimum(r-b,(r+g)/2-b)
    spread = np.max(target,axis=-1)-np.min(target,axis=-1)
    mask = (_smoothstep(.015,.10,warm_excess)
            *_smoothstep(.02,.12,spread)
            *_smoothstep(.10,.45,before_warm_y))
    if warm_strength > 0:
        target = before_warm_y[...,None]+(target-before_warm_y[...,None])*(1-warm_strength*mask[...,None])
    palette_y = colors @ Y_WEIGHTS
    diagnostics = {
        'mode':'display', 'strength':float(strength),'warmStrength':float(warm_strength),
        'floor':float(floor),'white':1.,'blackLightness':float(_lightness(floor)),
        'measuredPaletteMinimumY':float(palette_y.min()),
        'measuredPaletteMaximumY':float(palette_y.max()),
        'mapping':'source CIE L*(nominal sRGB Y) full-range fit; blended with previous linear baseline',
        'autoPercentiles':False,'paletteModified':False,'faceDetection':False,
        'colorimetry':'nominal camera-relative approximation, not calibrated spectral colorimetry',
        'chromaShortenedPixels':int(np.count_nonzero(gamut_scale < 1)),
        'warmMaskMaximum':float(mask.max()),
        'warmAffectedPixels':int(np.count_nonzero(mask*warm_strength > 0)),
        'warmMaximumYResidual':float(np.max(np.abs(target @ Y_WEIGHTS-before_warm_y))),
    }
    return target, diagnostics
