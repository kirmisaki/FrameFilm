"""Focused I006 web acceptance. Uses local HTTP and a headless browser; no devices."""
from pathlib import Path
from functools import partial
from http.server import ThreadingHTTPServer, SimpleHTTPRequestHandler
from threading import Thread
from playwright.sync_api import sync_playwright

out=Path('.output/ark/i006-web-preview');out.mkdir(parents=True,exist_ok=True)
server=ThreadingHTTPServer(('127.0.0.1',0),partial(SimpleHTTPRequestHandler,directory=str(Path('tools').resolve())))
Thread(target=server.serve_forever,daemon=True).start()
svg=b'<svg xmlns="http://www.w3.org/2000/svg" width="480" height="720"><defs><linearGradient id="g" x2="1" y2="1"><stop stop-color="#35291c"/><stop offset=".48" stop-color="#e2aa83"/><stop offset="1" stop-color="#558ab5"/></linearGradient></defs><rect width="480" height="720" fill="url(#g)"/><circle cx="240" cy="260" r="100" fill="#efb894"/></svg>'
with sync_playwright() as p:
    browser=p.chromium.launch(headless=True,channel='msedge')
    try:
        page=browser.new_page(viewport={'width':1280,'height':1000},reduced_motion='reduce')
        page.set_default_timeout(120000)
        errors=[];page.on('pageerror',lambda error:errors.append(str(error)))
        page.route('**/fonts.googleapis.com/**',lambda route:route.abort())
        page.route('**/fonts.gstatic.com/**',lambda route:route.abort())
        page.goto(f'http://127.0.0.1:{server.server_port}/ForFilm/',wait_until='networkidle')
        page.locator('[data-page="convert-page"]').click()
        page.locator('#ditherType').select_option('ark512I006')
        assert page.locator('#ark512Controls').is_visible()
        assert page.locator('#ark512ContinuousTone').is_checked()
        page.locator('#imageFile').set_input_files({'name':'I006-gradient.svg','mimeType':'image/svg+xml','buffer':svg})
        page.wait_for_function('ark512Result !== null')
        assert page.evaluate('[canvas.width,canvas.height]') == [720,480]
        assert page.evaluate('buildCurrentFilmFile().length')==691232
        # Live changes cancel stale work and the latest contrast value wins.
        page.evaluate("document.getElementById('contrast').value='1.2';document.getElementById('contrast').dispatchEvent(new Event('input',{bubbles:true}))")
        page.wait_for_function('ark512Result !== null')
        assert '模拟' in page.locator('#ark512Status').inner_text()
        page.screenshot(path=str(out/'desktop.png'),full_page=True)
        with page.expect_download() as png_download:
            page.get_by_role('button',name='导出模拟 PNG').click()
        png_download.value.save_as(str(out/'I006-simulation.png'))
        with page.expect_download() as film_download:
            page.locator('button[onclick="downloadFilmFile()"]').click()
        film_path=out/'I006-experiment.film';film_download.value.save_as(str(film_path))
        assert film_path.stat().st_size==691232
        page.evaluate('updateImage()')
        page.get_by_role('button',name='取消计算',exact=True).click()
        assert page.evaluate('ark512Result === null')
        assert '已取消' in page.locator('#ark512Status').inner_text()
        page.evaluate('updateImage()')
        page.wait_for_function('ark512Result !== null')
        original=page.evaluate('Array.from(buildCurrentFilmFile().slice(0,48))')
        page.locator('#ark512Controls input[type=file]').set_input_files(str(film_path.resolve()))
        page.wait_for_function('ark512Imported !== null')
        assert page.evaluate('Array.from(buildCurrentFilmFile().slice(0,48))')==original
        page.evaluate("uploadFilmFileViaBle('I006-test.film',buildCurrentFilmFile())")
        assert page.get_by_text('当前设备固件尚未确认支持 512 色预览文件',exact=False).count()>0
        page.set_viewport_size({'width':390,'height':844})
        page.locator('.message').evaluate_all('(elements)=>elements.forEach(e=>e.remove())')
        page.screenshot(path=str(out/'mobile.png'),full_page=True)
        page.locator('#canvas').scroll_into_view_if_needed()
        page.screenshot(path=str(out/'mobile-preview.png'),full_page=True)
        assert page.evaluate('document.documentElement.scrollWidth <= innerWidth')
        page.locator('#ditherType').select_option('floydSteinberg')
        assert page.locator('#ark512Controls').is_hidden()
        assert not errors,errors
        print('PASS I006 offline selection, simulation, live adjustment, cancellation, PNG/film export, original-code import, unsupported upload gate, desktop/mobile layout')
    finally:
        browser.close()
        server.shutdown();server.server_close()
