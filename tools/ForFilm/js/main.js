// 页面导航：切换页面 + 同步头部标题
var PAGE_TITLES = {
    'bluetooth-page': '设备连接',
    'frame-page': 'Frame 制作',
    'convert-page': '照片转换',
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

// 初始化应用
function initApp() {
    initNavigation();
    initBluetooth();
    initUsb();
    initKeyboardSetting();
    initConvertTool();
}

// 页面加载完成后初始化
window.addEventListener('DOMContentLoaded', initApp);
