import numpy as np
from abc512_render import linear, quantize


def test_dark_responses_survive():
    colors = linear([[20,20,20],[40,40,40],[80,80,80]]) / linear([200,200,200])
    target = colors.reshape(1,3,3)
    for diffusion in (False, True):
        np.testing.assert_array_equal(quantize(target,colors,diffusion),[[0,1,2]])


def test_serpentine_exact_palette_and_range():
    colors = np.array([[0.,0.,0.],[1.,1.,1.]])
    target = np.tile(np.array([[[0.,0.,0.],[1.,1.,1.]]]),(3,1,1))
    np.testing.assert_array_equal(quantize(target,colors,True),[[0,1]]*3)
    result = quantize(np.full((3,4,3),.5),colors,True)
    assert set(result.flat) <= {0,1}


def test_shared_floor_includes_abc_dark_range():
    from abc512_render import shared_floor
    data = {'control':[{'cameraRGB':[80,80,80]}], 'entries':[{'cameraRGB':[20,20,20]}]}
    white = linear([200,200,200])
    floor = shared_floor(data,white)
    assert floor < float((linear([80,80,80])/white).mean())
    assert np.isclose(floor,float((linear([20,20,20])/white).mean()))
    target = floor + (1-floor)*linear(np.zeros((1,1,3)))
    colors = linear([[20,20,20],[80,80,80]])/white
    assert quantize(target,colors)[0,0] == 0
