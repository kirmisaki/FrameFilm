// Frame 页：入口卡片 ↔ 全屏二级页
//
// 复用 frame.js 已有的 .frame-tab / .frame-tab-content 切换契约，
// 这里只负责「卡片网格收起 / 二级页展开 / 返回」这层展示逻辑。

(function () {
    'use strict';

    var DETAIL_TITLES = {
        'frame-upload': '拾光',
        'frame-camera': '定影',
        'frame-quote': '一言',
        'frame-batch': '批量上传'
    };

    function initFrameNav() {
        var grid = document.getElementById('frameCardGrid');
        var detail = document.getElementById('frameDetail');
        var backBtn = document.getElementById('frameDetailBack');
        var nameEl = document.getElementById('frameDetailName');
        var scroller = document.getElementById('page-content');
        if (!grid || !detail || !backBtn || !nameEl) return;

        function openDetail(key) {
            grid.classList.add('is-hidden');
            detail.classList.add('is-open');
            nameEl.textContent = DETAIL_TITLES[key] || '';
            if (scroller) scroller.scrollTop = 0;
        }

        function closeDetail() {
            document.querySelectorAll('.frame-tab').forEach(function (tab) {
                tab.classList.remove('active');
            });
            document.querySelectorAll('.frame-tab-content').forEach(function (content) {
                content.classList.remove('active');
            });
            detail.classList.remove('is-open');
            grid.classList.remove('is-hidden');
            if (scroller) scroller.scrollTop = 0;
        }

        // frame.js 的 initFrameTabSwitch 会先把卡片切到 active，
        // 这里在其之后补上「收起网格 + 展开二级页」
        document.querySelectorAll('.frame-tab').forEach(function (tab) {
            tab.addEventListener('click', function () {
                openDetail(tab.getAttribute('data-frame-tab'));
            });
        });

        backBtn.addEventListener('click', closeDetail);
    }

    window.addEventListener('DOMContentLoaded', initFrameNav);
})();
