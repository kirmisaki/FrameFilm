import numpy as np
import pytest
from epd_source_fit import fit_source, _linear, Y_WEIGHTS

COLORS=np.array([[.02,.02,.02],[1.,1.,1.]])


def test_zero_controls_exact_baseline():
    source=np.arange(72,dtype=np.uint8).reshape(4,6,3)
    out,d=fit_source(source,COLORS,.02)
    np.testing.assert_array_equal(out,.02+.98*_linear(source))
    assert not d['paletteModified']


def test_gray_ramp_monotonic_endpoints_and_no_tail_collapse():
    src=np.repeat(np.arange(256)[None,:,None],3,axis=2)
    out,_=fit_source(src,COLORS,.02,strength=1)
    np.testing.assert_allclose(out[0,0],.02,atol=1e-14)
    np.testing.assert_allclose(out[0,-1],1,atol=1e-14)
    assert np.all(np.diff(out[0,:,0])>0)


def test_warm_preserves_y_neutral_blue_unchanged():
    source=np.array([[[230,175,105],[180,180,180],[65,120,235]]])
    base,_=fit_source(source,COLORS,.02,strength=1)
    warm,d=fit_source(source,COLORS,.02,strength=1,warm_strength=.8)
    np.testing.assert_allclose(warm @ Y_WEIGHTS,base @ Y_WEIGHTS,atol=1e-14)
    np.testing.assert_array_equal(warm[:,1:],base[:,1:])
    assert np.ptp(warm[0,0])<np.ptp(base[0,0])
    assert d['warmAffectedPixels']==1


def test_flat_and_saturated_input_finite_bounded_palette_unchanged():
    colors=COLORS.copy()
    src=np.array([[[255,0,0],[0,255,0],[0,0,255],[0,0,0]]])
    for image in (src,np.full((2,3,3),128)):
        out,_=fit_source(image,colors,.02,strength=1,warm_strength=1)
        assert np.isfinite(out).all() and out.min()>=-1e-14 and out.max()<=1+1e-14
    np.testing.assert_array_equal(colors,COLORS)


def test_validation():
    with pytest.raises(ValueError,match='strength'):
        fit_source(np.zeros((1,1,3)),COLORS,.02,strength=2)
    with pytest.raises(ValueError,match='finite'):
        fit_source(np.full((1,1,3),np.nan),COLORS,.02)
