import numpy as np
import pytest
from abc_spatial_mix import decompose, sample_mixture, split_phases


def test_tetrahedron_reconstruction_and_outside():
    colors=np.array([[0,0,0],[1,0,0],[0,1,0],[0,0,1],[0,0,0]],float)
    target=np.array([[[.1,.2,.3],[2.,2.,2.]]])
    v,w,m,d=decompose(target,colors)
    np.testing.assert_allclose(w.sum(axis=-1),1)
    assert np.all(w>=0)
    np.testing.assert_allclose(np.einsum('...i,...ij->...j',w,colors[v]),m)
    np.testing.assert_allclose(m[0,0],target[0,0])
    assert np.all(m>=0) and np.all(m.sum(axis=-1)<=1+1e-9)
    assert d['outsideCount']==1 and d['uniqueGeometryCount']==4
    assert 4 not in v


def test_flat_patch_proportions_and_roundtrip():
    v=np.broadcast_to(np.arange(4),(8,8,4))
    w=np.broadcast_to(np.array([.125,.25,.375,.25]),(8,8,4))
    selected=sample_mixture(v,w)
    np.testing.assert_array_equal(np.bincount(selected.ravel()),[8,16,24,16])
    codes=np.arange(512).reshape(16,32)
    a,b,c=split_phases(codes)
    assert a.dtype==b.dtype==c.dtype==np.uint8
    np.testing.assert_array_equal((a.astype(int)<<6)|(b.astype(int)<<3)|c,codes)


def test_invalid_geometry():
    with pytest.raises(ValueError,match='3D hull'):
        decompose(np.zeros((1,1,3)),np.zeros((4,3)))
    with pytest.raises(ValueError,match='finite'):
        decompose(np.full((1,1,3),np.nan),np.eye(3))


def test_phase_masks_keep_joint_tuple_correlation():
    # Two measured endpoint sequences; independent per-phase masks would create
    # six other, unrequested sequences even though their marginals looked right.
    vertices = np.broadcast_to(np.array([83, 428, 83, 83]), (8, 8, 4))
    weights = np.broadcast_to(np.array([.5, .5, 0., 0.]), (8, 8, 4))
    chosen = sample_mixture(vertices, weights)
    a, b, c = split_phases(chosen)
    actual = set(zip(a.ravel(), b.ravel(), c.ravel()))
    assert actual == {(1, 2, 3), (6, 5, 4)}
    assert np.count_nonzero(chosen == 83) == np.count_nonzero(chosen == 428) == 32


def test_bayer16_resolves_finer_weights():
    vertices = np.broadcast_to(np.array([0, 1, 0, 0]), (16, 16, 4))
    weights = np.broadcast_to(np.array([1/256, 255/256, 0., 0.]), (16, 16, 4))
    assert np.count_nonzero(sample_mixture(vertices, weights, 16) == 0) == 1
    assert np.count_nonzero(sample_mixture(vertices, weights, 8) == 0) == 0
