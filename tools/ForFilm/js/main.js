// 页面导航功能
function initNavigation() {
    const navItems = document.querySelectorAll('.nav-item');
    const pages = document.querySelectorAll('.page');
    const tabs = document.querySelectorAll('.tab');
    const tabContents = document.querySelectorAll('.tab-content');

    // 底部导航切换
    navItems.forEach(item => {
        item.addEventListener('click', function(e) {
            e.preventDefault();
            const pageId = this.getAttribute('data-page');

            // 更新导航状态
            navItems.forEach(nav => nav.classList.remove('active'));
            this.classList.add('active');

            // 更新页面显示
            pages.forEach(page => page.classList.remove('active'));
            document.getElementById(pageId).classList.add('active');
        });
    });

    // 标签页切换
    tabs.forEach(tab => {
        tab.addEventListener('click', function() {
            const tabId = this.getAttribute('data-tab');

            // 更新标签状态
            tabs.forEach(t => t.classList.remove('active'));
            this.classList.add('active');

            // 更新内容显示
            tabContents.forEach(content => content.classList.remove('active'));
            document.getElementById(tabId).classList.add('active');
        });
    });
}

// 初始化应用
function initApp() {
    initNavigation();
    initBluetooth();
    initConvertTool();
}

// 页面加载完成后初始化
window.addEventListener('DOMContentLoaded', initApp);