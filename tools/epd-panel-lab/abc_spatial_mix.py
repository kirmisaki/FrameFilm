"""Joint-state spatial mixtures using SciPy geometry, not panel waveform changes.

References (mathematical ideas only, no copied source/assets):
https://docs.scipy.org/doc/scipy/reference/generated/scipy.spatial.Delaunay.html
https://docs.scipy.org/doc/scipy/reference/generated/scipy.spatial.ConvexHull.html
https://github.com/Frans-Willem/epd-dither/blob/main/docs/decomposition.md
"""
import numpy as np
from scipy.spatial import ConvexHull, Delaunay, QhullError


def decompose(target, colors):
    """Return original entry indices, convex weights, mapped target and diagnostics.

    Outside points are compressed radially toward the mean of unique palette
    points until they meet the hull. This is geometry, not hue preservation or
    calibrated color correction. Exact duplicates share their first entry.
    Invalid/non-finite/less-than-3D palettes raise descriptive ValueError.
    """
    target = np.asarray(target, dtype=float)
    colors = np.asarray(colors, dtype=float)
    if target.ndim != 3 or target.shape[-1] != 3:
        raise ValueError('target must have shape HxWx3')
    if colors.ndim != 2 or colors.shape[1] != 3:
        raise ValueError('colors must have shape Nx3')
    if not np.isfinite(target).all() or not np.isfinite(colors).all():
        raise ValueError('target and colors must contain only finite values')
    unique, first = np.unique(colors, axis=0, return_index=True)
    rank = np.linalg.matrix_rank(unique-unique.mean(axis=0)) if len(unique) else 0
    if len(unique) < 4 or rank < 3:
        raise ValueError(f'palette requires a 3D hull: unique={len(unique)}, rank={rank}')
    try:
        hull = ConvexHull(unique)
        tetra = Delaunay(unique)
    except QhullError as exc:
        raise ValueError('palette geometry is numerically degenerate; cannot triangulate') from exc
    center = unique.mean(axis=0)
    points = target.reshape(-1,3)
    directions = points-center
    normals, offsets = hull.equations[:,:3], hull.equations[:,3]
    # Half-space n.p+b <= 0; intersect each ray with all forward-facing planes.
    denominator = directions @ normals.T
    numerator = -(normals @ center+offsets)
    ratios = np.full_like(denominator, np.inf)
    np.divide(numerator[None,:], denominator, out=ratios, where=denominator > 0)
    scale = np.minimum(1., ratios.min(axis=1))
    outside = scale < 1.
    # Move projected boundary points a tiny amount inside for robust find_simplex.
    scale[outside] *= 1.-1e-10
    mapped = center+directions*scale[:,None]
    simplex = tetra.find_simplex(mapped, tol=1e-9)
    if np.any(simplex < 0):
        raise ValueError('mapped points could not be located in palette tetrahedra')
    transform = tetra.transform[simplex]
    bary = np.einsum('nij,nj->ni',transform[:,:3,:],mapped-transform[:,3,:])
    weights = np.column_stack((bary,1-bary.sum(axis=1)))
    if weights.min(initial=0) < -1e-7:
        raise ValueError('triangulation produced materially negative mixture weights')
    weights = np.maximum(weights,0)
    weights /= weights.sum(axis=1,keepdims=True)
    indices = first[tetra.simplices[simplex]]
    reconstructed = np.einsum('ni,nij->nj',weights,colors[indices])
    shape = target.shape[:2]
    return (indices.reshape(*shape,4), weights.reshape(*shape,4),
            reconstructed.reshape(target.shape),
            {'originalCount':len(colors),'uniqueGeometryCount':len(unique),
             'uniqueToOriginal':first.tolist(),'referencePoint':center.tolist(),
             'outsideCount':int(outside.sum()),'minimumRadialScale':float(scale.min(initial=1)),
             'mapping':'radial compression to measured convex hull; not hue-preserving or calibrated',
             'maximumReconstructionResidual':float(np.max(np.abs(reconstructed-mapped),initial=0))})


def sample_mixture(vertices, weights, bayer_size=8):
    """Standard Bayer8/16 sampling of joint states (not blue noise)."""
    if bayer_size not in (8, 16):
        raise ValueError('bayer_size must be 8 or 16')
    vertices = np.asarray(vertices)
    weights = np.asarray(weights,dtype=float)
    if vertices.shape != weights.shape or weights.ndim != 3 or weights.shape[-1] != 4:
        raise ValueError('vertices and weights must have matching HxWx4 shapes')
    if not np.isfinite(weights).all() or np.any(weights < 0) or not np.allclose(weights.sum(axis=-1),1):
        raise ValueError('weights must be finite nonnegative and sum to one')
    bayer = np.array([[0]])
    for _ in range(bayer_size.bit_length()-1):
        bayer = np.block([[4*bayer,4*bayer+2],[4*bayer+3,4*bayer+1]])
    h,w = weights.shape[:2]
    threshold = (bayer[np.arange(h)[:,None]%bayer_size,np.arange(w)[None,:]%bayer_size]+.5)/(bayer_size*bayer_size)
    slot = np.minimum((threshold[...,None] >= np.cumsum(weights,axis=-1)).sum(axis=-1),3)
    return np.take_along_axis(vertices,slot[...,None],axis=-1)[...,0]


def split_phases(codes):
    """Split ABC9 code (A<<6 | B<<3 | C) into correlated physical phase maps."""
    codes = np.asarray(codes)
    if not np.issubdtype(codes.dtype,np.integer) or np.any(codes < 0) or np.any(codes > 511):
        raise ValueError('codes must be integers in 0..511')
    return tuple(((codes >> shift)&7).astype(np.uint8) for shift in (6,3,0))
