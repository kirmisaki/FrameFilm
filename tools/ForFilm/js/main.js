// 页面导航：切换页面 + 同步头部标题
/* ForFilm 网页版本号：发布时手动递增 */
var APP_VERSION = '1.0.0';
/* 提交号：优先从 GitHub API 读取 main 的最新提交，离线/限流时用下面的回退值 */
var APP_REPO = 'kirmisaki/FrameFilm';
var APP_COMMIT_FALLBACK = '0c4b730';

var PAGE_TITLES = {
    'bluetooth-page': '设备连接',
    'frame-page': 'Frame 制作',
    'convert-page': '照片转换',
    'anim-page': '动画工坊',
    'pass-page': '个人通行证',
    'config-page': '设备设置'
};

function initNavigation() {
    const navItems = document.querySelectorAll('.nav-item');
    const pages = document.querySelectorAll('.page');
    const titleEl = document.getElementById('appTitle');
    const scroller = document.getElementById('page-content');

    navItems.forEach(item => {
        item.addEventListener('click', function (e) {
            e.preventDefault();
            const pageId = this.getAttribute('data-page');
            if (!pageId) return;

            navItems.forEach(nav => nav.classList.remove('active'));
            this.classList.add('active');

            pages.forEach(page => page.classList.remove('active'));
            const target = document.getElementById(pageId);
            if (target) target.classList.add('active');

            if (titleEl && PAGE_TITLES[pageId]) {
                titleEl.textContent = PAGE_TITLES[pageId];
            }
            if (scroller) scroller.scrollTop = 0;
            window.scrollTo(0, 0);
        });
    });
}

/* 页脚提交号：先显示内置回退值，联网成功后再替换为仓库最新提交 */
async function syncCommitId() {
    const el = document.getElementById('appCommit');
    let sha = APP_COMMIT_FALLBACK;
    try {
        const rsp = await fetch('https://api.github.com/repos/' + APP_REPO + '/commits/main', {
            headers: { Accept: 'application/vnd.github+json' }
        });
        if (!rsp.ok) throw new Error('HTTP ' + rsp.status);
        const data = await rsp.json();
        if (data && typeof data.sha === 'string' && data.sha) sha = data.sha.slice(0, 7);
    } catch (e) { /* 离线或接口受限时保留回退值 */ }
    if (!el) return;
    el.textContent = sha;
    el.href = 'https://github.com/' + APP_REPO + '/commit/' + sha;
}

// 初始化应用
function initApp() {
    const versionEl = document.getElementById('appVersion');
    if (versionEl) versionEl.textContent = 'v' + APP_VERSION;
    syncCommitId();
    initNavigation();
    initBluetooth();
    initUsb();
    initKeyboardSetting();
    initConvertTool();
}

// 页面加载完成后初始化
window.addEventListener('DOMContentLoaded', initApp);
