// 风格切换功能
function initStyleSwitcher() {
    const styleSheet = document.getElementById('style-sheet');
    const styleSelector = document.getElementById('style-selector');
    
    // 从 localStorage 加载已保存的样式
    const savedStyle = localStorage.getItem('selectedStyle');
    if (savedStyle) {
        styleSheet.setAttribute('href', `css/${savedStyle}`);
        // 更新选择框状态
        if (styleSelector) {
            styleSelector.value = savedStyle;
        }
    }
    
    // 添加变更事件
    if (styleSelector) {
        styleSelector.addEventListener('change', function() {
            const styleFile = this.value;
            styleSheet.setAttribute('href', `css/${styleFile}`);
            
            // 保存选择到 localStorage
            localStorage.setItem('selectedStyle', styleFile);
        });
    }
}

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
    initStyleSwitcher();
    initNavigation();
    initBluetooth();
    initConvertTool();
}

// 页面加载完成后初始化
window.addEventListener('DOMContentLoaded', initApp);