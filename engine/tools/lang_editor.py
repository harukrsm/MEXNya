#!/usr/bin/env python3
"""Source of the MEXNya Editor's UI text in every language. Writes
engine/resources/lang/editor.<lang>.json. Edit the table here, then run:

    python3 engine/tools/lang_editor.py

Each row: key, English, Simplified Chinese, Traditional Chinese, Japanese, Korean.
Keys are the code's names for strings and never change with the language; printf
conversions (%s %d %.1f) must match English in every column (the engine rejects a
translation that differs, and scripts/check_i18n.py fails on it).
"""
import json
import os

LANGS = ["en", "zh-Hans", "zh-Hant", "ja", "ko"]

ROWS = [
    # top bar and pages
    ("tab.scene", "Scene", "场景", "場景", "シーン", "씬"),
    ("tab.graph", "Graph", "逻辑图", "邏輯圖", "グラフ", "그래프"),
    ("tab.build", "Build", "构建", "建置", "ビルド", "빌드"),
    ("tab.git", "Git", "Git", "Git", "Git", "Git"),
    ("tab.agent", "Agent", "Agent", "Agent", "エージェント", "에이전트"),
    ("tab.plugins", "Plugins", "插件", "外掛", "プラグイン", "플러그인"),
    ("top.unsaved", "● Unsaved", "● 未保存", "● 未儲存", "● 未保存", "● 저장 안 됨"),
    ("top.save", "Save", "保存", "儲存", "保存", "저장"),
    ("top.undo", "Undo", "撤销", "復原", "元に戻す", "실행 취소"),
    ("top.redo", "Redo", "重做", "重做", "やり直し", "다시 실행"),
    ("top.settings", "Settings", "设置", "設定", "設定", "설정"),
    # status line
    ("status.saved", "Saved", "已保存", "已儲存", "保存しました", "저장됨"),
    ("status.ui_scale", "UI scale %d×", "界面缩放 %d×", "介面縮放 %d×", "UI の拡大率 %d×", "UI 배율 %d×"),
    ("status.reloaded", "Reloaded", "已重新载入", "已重新載入", "再読み込みしました", "다시 불러옴"),
    ("status.reloaded_external", "Files changed on disk; reloaded", "文件已在外部更改，已重新载入",
     "檔案已在外部變更，已重新載入", "ファイルが外部で変更されたため再読み込みしました", "파일이 외부에서 변경되어 다시 불러옴"),
    ("status.undo", "Undo %s", "撤销 %s", "復原 %s", "元に戻す: %s", "실행 취소: %s"),
    ("status.redo", "Redo %s", "重做 %s", "重做 %s", "やり直し: %s", "다시 실행: %s"),
    ("status.copied", "Copied %s", "已复制 %s", "已複製 %s", "%s をコピーしました", "%s 복사됨"),
    ("status.note_saved", "Note saved", "批注已保存", "註記已儲存", "注釈を保存しました", "주석 저장됨"),
    ("status.no_models", "No models in assets/models", "assets/models 中没有模型", "assets/models 中沒有模型",
     "assets/models にモデルがありません", "assets/models에 모델이 없습니다"),
    ("status.props_invalid", "Properties must be a JSON object: %s", "属性必须是 JSON 对象：%s",
     "屬性必須是 JSON 物件：%s", "プロパティは JSON オブジェクトにしてください: %s", "속성은 JSON 객체여야 합니다: %s"),
    # tools
    ("tool.select", "Select (Q)", "选择 (Q)", "選取 (Q)", "選択 (Q)", "선택 (Q)"),
    ("tool.move", "Move (W)", "移动 (W)", "移動 (W)", "移動 (W)", "이동 (W)"),
    ("tool.rotate", "Rotate (E)", "旋转 (E)", "旋轉 (E)", "回転 (E)", "회전 (E)"),
    ("tool.scale", "Scale (R)", "缩放 (R)", "縮放 (R)", "拡大縮小 (R)", "크기 조절 (R)"),
    ("tool.box", "Rectangle note (B)", "矩形批注 (B)", "矩形註記 (B)", "矩形の注釈 (B)", "사각형 주석 (B)"),
    ("tool.circle", "Circle note (C)", "圆形批注 (C)", "圓形註記 (C)", "円の注釈 (C)", "원형 주석 (C)"),
    ("tool.arrow", "Arrow note (A)", "箭头批注 (A)", "箭頭註記 (A)", "矢印の注釈 (A)", "화살표 주석 (A)"),
    ("tool.pin", "Pin note (N)", "图钉批注 (N)", "圖釘註記 (N)", "ピンの注釈 (N)", "핀 주석 (N)"),
    ("tool.pen", "Freehand note (P)", "手绘批注 (P)", "手繪註記 (P)", "フリーハンドの注釈 (P)", "자유 곡선 주석 (P)"),
    # panels
    ("panel.scenes", "Scenes", "场景", "場景", "シーン", "씬"),
    ("panel.hierarchy", "Hierarchy", "层级", "階層", "階層", "계층"),
    ("panel.inspector", "Inspector", "检查器", "檢閱器", "インスペクター", "인스펙터"),
    ("panel.notes", "Notes %d/%d", "批注 %d/%d", "註記 %d/%d", "注釈 %d/%d", "주석 %d/%d"),
    ("menu.new_scene_3d", "New 3D Scene", "新建 3D 场景", "新增 3D 場景", "新規 3D シーン", "새 3D 씬"),
    ("menu.new_scene_2d", "New 2D Scene", "新建 2D 场景", "新增 2D 場景", "新規 2D シーン", "새 2D 씬"),
    ("menu.new_tunnel", "New Tunnel", "新建 Tunnel", "新增 Tunnel", "新規 Tunnel", "새 Tunnel"),
    ("menu.rename", "Rename", "重命名", "重新命名", "名前を変更", "이름 바꾸기"),
    ("menu.move_up", "Move Up", "上移", "上移", "上へ移動", "위로 이동"),
    ("menu.move_down", "Move Down", "下移", "下移", "下へ移動", "아래로 이동"),
    ("menu.copy_id", "Copy ID", "复制 ID", "複製 ID", "ID をコピー", "ID 복사"),
    ("menu.delete", "Delete", "删除", "刪除", "削除", "삭제"),
    ("menu.duplicate", "Duplicate", "创建副本", "建立副本", "複製", "복제"),
    ("menu.focus", "Frame", "聚焦", "聚焦", "フレーム", "프레임"),
    ("type.box", "Cube", "立方体", "立方體", "立方体", "큐브"),
    ("type.plane", "Plane", "平面", "平面", "平面", "평면"),
    ("type.sphere", "Sphere", "球体", "球體", "球", "구"),
    ("type.cylinder", "Cylinder", "圆柱体", "圓柱體", "円柱", "원기둥"),
    ("type.cone", "Cone", "圆锥体", "圓錐體", "円錐", "원뿔"),
    ("type.model", "Model", "模型", "模型", "モデル", "모델"),
    ("type.light", "Light", "灯光", "燈光", "ライト", "조명"),
    ("type.marker", "Marker", "标记点", "標記點", "マーカー", "마커"),
    ("type.camera", "Camera", "摄像机", "攝影機", "カメラ", "카메라"),
    ("type.rect", "Rectangle", "矩形", "矩形", "矩形", "사각형"),
    ("type.text", "Text", "文本", "文字", "テキスト", "텍스트"),
    ("type.button", "Button", "按钮", "按鈕", "ボタン", "버튼"),
    ("type.image", "Image", "图像", "影像", "画像", "이미지"),
    ("type.panel", "Panel", "面板", "面板", "パネル", "패널"),
    # inspector fields
    ("field.name", "Name", "名称", "名稱", "名前", "이름"),
    ("field.rect", "Rect", "矩形", "矩形", "矩形", "사각형"),
    ("field.text", "Text", "文本", "文字", "テキスト", "텍스트"),
    ("field.image", "Image", "图像", "影像", "画像", "이미지"),
    ("field.position", "Position", "位置", "位置", "位置", "위치"),
    ("field.rotation", "Rotation", "旋转", "旋轉", "回転", "회전"),
    ("field.scale", "Scale", "缩放", "縮放", "スケール", "크기"),
    ("field.model", "Model", "模型", "模型", "モデル", "모델"),
    ("field.color", "Color", "颜色", "顏色", "色", "색상"),
    ("field.visible", "Visible", "可见", "可見", "表示", "표시"),
    ("field.props", "Properties (JSON)", "属性 (JSON)", "屬性 (JSON)", "プロパティ (JSON)", "속성 (JSON)"),
    ("field.script", "Script", "脚本", "腳本", "スクリプト", "스크립트"),
    ("field.canvas", "Canvas", "画布", "畫布", "キャンバス", "캔버스"),
    ("field.fog_range", "Fog range", "雾距", "霧距", "フォグ距離", "안개 거리"),
    ("field.fog_color", "Fog color", "雾色", "霧色", "フォグ色", "안개 색"),
    ("field.fog_enabled", "Fog", "雾效", "霧效", "フォグ", "안개"),
    ("field.ports", "Ports", "端口", "連接埠", "ポート", "포트"),
    ("port.in", "In", "入", "入", "入力", "입력"),
    ("port.out", "Out", "出", "出", "出力", "출력"),
    # notes
    ("note.new", "New Note", "新建批注", "新增註記", "新規注釈", "새 주석"),
    ("note.targets", "Objects:", "对象：", "物件：", "オブジェクト:", "오브젝트:"),
    ("note.reply", "Reply", "回复", "回覆", "返信", "답글"),
    ("note.resolve", "Resolve", "解决", "解決", "解決", "해결"),
    ("note.reopen", "Reopen", "重新打开", "重新開啟", "再オープン", "다시 열기"),
    ("button.save", "Save", "保存", "儲存", "保存", "저장"),
    ("button.cancel", "Cancel", "取消", "取消", "キャンセル", "취소"),
    # console tabs
    ("console.log", "Log", "日志", "記錄", "ログ", "로그"),
    ("console.ops", "Operations", "操作", "操作", "操作", "작업"),
    ("console.build", "Build", "构建", "建置", "ビルド", "빌드"),
    ("console.lua", "Lua", "Lua", "Lua", "Lua", "Lua"),
    # viewport
    ("view.persp", "Persp", "透视", "透視", "透視", "원근"),
    ("view.top", "Top", "顶", "頂", "上", "위"),
    ("view.front", "Front", "前", "前", "前", "앞"),
    ("view.right", "Right", "右", "右", "右", "오른쪽"),
    ("view.ortho", "Ortho", "正交", "正交", "平行投影", "직교"),
    ("view.no_scene", "No scene", "无场景", "無場景", "シーンがありません", "씬 없음"),
    # graph
    ("graph.properties", "Properties", "属性", "屬性", "プロパティ", "속성"),
    ("graph.from", "From %s", "从 %s", "從 %s", "始点 %s", "시작 %s"),
    ("graph.to", "To %s", "到 %s", "到 %s", "終点 %s", "끝 %s"),
    ("graph.note", "Note", "备注", "備註", "メモ", "메모"),
    ("graph.delete_line", "Delete Line", "删除连线", "刪除連線", "線を削除", "선 삭제"),
    ("graph.new_port", "New port", "新端口", "新連接埠", "新規ポート", "새 포트"),
    ("graph.add_port", "Add Port", "添加端口", "新增連接埠", "ポートを追加", "포트 추가"),
    ("graph.open_scene", "Open Scene", "打开场景", "開啟場景", "シーンを開く", "씬 열기"),
    ("graph.delete_tunnel", "Delete Tunnel", "删除 Tunnel", "刪除 Tunnel", "Tunnel を削除", "Tunnel 삭제"),
    # build
    ("build.targets", "Targets", "目标", "目標", "ターゲット", "대상"),
    ("build.target", "Build %s", "构建 %s", "建置 %s", "%s をビルド", "%s 빌드"),
    ("build.all", "Build All", "全部构建", "全部建置", "すべてビルド", "모두 빌드"),
    ("build.test_package", "Test & Package", "测试与打包", "測試與封裝", "テストとパッケージ", "테스트 및 패키지"),
    ("build.test", "Run Tests", "运行测试", "執行測試", "テストを実行", "테스트 실행"),
    ("build.package", "Package %s", "打包 %s", "封裝 %s", "%s をパッケージ", "%s 패키지"),
    ("build.run", "Run", "运行", "執行", "実行", "실행"),
    ("build.run_game", "Run Game", "运行游戏", "執行遊戲", "ゲームを実行", "게임 실행"),
    ("build.doctor", "Check Toolchain", "检查工具链", "檢查工具鏈", "ツールチェーンを確認", "툴체인 확인"),
    ("build.output", "Output", "输出", "輸出", "出力", "출력"),
    ("build.done", "done", "完成", "完成", "完了", "완료"),
    ("build.failed", "failed", "失败", "失敗", "失敗", "실패"),
    ("build.result", "%s %s (%.1fs, exit %d)", "%s %s（%.1f 秒，退出码 %d）", "%s %s（%.1f 秒，結束代碼 %d）",
     "%s %s（%.1f 秒、終了コード %d）", "%s %s (%.1f초, 종료 코드 %d)"),
    ("build.state", "%s: %s", "%s：%s", "%s：%s", "%s: %s", "%s: %s"),
    ("build.started", "Started %s", "已启动 %s", "已啟動 %s", "%s を起動しました", "%s 실행됨"),
    # git
    ("git.title", "Version Control", "版本控制", "版本控制", "バージョン管理", "버전 관리"),
    ("git.not_repo", "Not a Git repository", "当前目录不是 Git 仓库", "目前目錄不是 Git 儲存庫",
     "Git リポジトリではありません", "Git 저장소가 아닙니다"),
    ("git.init", "Initialize Repository", "初始化仓库", "初始化儲存庫", "リポジトリを初期化", "저장소 초기화"),
    ("git.branch", "Branch", "分支", "分支", "ブランチ", "브랜치"),
    ("git.refresh", "Refresh", "刷新", "重新整理", "更新", "새로 고침"),
    ("git.fetch", "Fetch", "抓取", "擷取", "フェッチ", "가져오기"),
    ("git.pull", "Pull", "拉取", "拉取", "プル", "풀"),
    ("git.push", "Push", "推送", "推送", "プッシュ", "푸시"),
    ("git.confirm_push", "Confirm Push", "确认推送", "確認推送", "プッシュを確認", "푸시 확인"),
    ("git.push_hint", "Click again to push to the remote", "再次点击以推送到远程仓库", "再次點擊以推送到遠端儲存庫",
     "もう一度クリックするとリモートにプッシュします", "다시 클릭하면 원격 저장소로 푸시합니다"),
    ("git.changes", "Changes", "更改", "變更", "変更", "변경 사항"),
    ("git.clean", "No changes", "无更改", "無變更", "変更なし", "변경 사항 없음"),
    ("git.message", "Commit message", "提交信息", "提交訊息", "コミットメッセージ", "커밋 메시지"),
    ("git.commit_all", "Commit All", "提交全部更改", "提交所有變更", "すべてコミット", "모두 커밋"),
    ("git.need_message", "Enter a commit message", "请填写提交信息", "請填寫提交訊息",
     "コミットメッセージを入力してください", "커밋 메시지를 입력하세요"),
    ("git.committed", "Committed", "已提交", "已提交", "コミットしました", "커밋됨"),
    ("git.commit_failed", "Commit failed; see the build output", "提交失败，详见构建输出", "提交失敗，詳見建置輸出",
     "コミットに失敗しました。ビルド出力を確認してください", "커밋 실패. 빌드 출력을 확인하세요"),
    ("git.history", "History", "历史", "歷史", "履歴", "기록"),
    ("git.no_diff", "(no text diff)", "（无文本差异）", "（無文字差異）", "（テキストの差分なし）", "(텍스트 차이 없음)"),
    # agent
    ("agent.command_line", "Command line", "命令行", "命令列", "コマンドライン", "명령줄"),
    ("agent.copy_setup", "Copy Setup Instructions", "复制接入说明", "複製接入說明", "接続手順をコピー", "연결 안내 복사"),
    ("agent.copied", "Copied to the clipboard", "已复制到剪贴板", "已複製到剪貼簿", "クリップボードにコピーしました", "클립보드에 복사됨"),
    ("agent.regen", "Regenerate AGENTS.md", "重新生成 AGENTS.md", "重新產生 AGENTS.md", "AGENTS.md を再生成", "AGENTS.md 다시 생성"),
    ("agent.regenerated", "AGENTS.md regenerated", "已重新生成 AGENTS.md", "已重新產生 AGENTS.md",
     "AGENTS.md を再生成しました", "AGENTS.md를 다시 생성함"),
    ("agent.requests", "Requests this session: %d", "本次会话请求：%d", "本次工作階段請求：%d",
     "このセッションのリクエスト: %d", "이번 세션 요청: %d"),
    ("agent.errors", "Errors %d", "错误 %d", "錯誤 %d", "エラー %d", "오류 %d"),
    ("agent.none", "No connections", "无连接", "無連線", "接続なし", "연결 없음"),
    ("agent.log", "Operation Log", "操作日志", "操作記錄", "操作ログ", "작업 로그"),
    # plugins
    ("plugins.reload", "Reload", "重新加载", "重新載入", "再読み込み", "다시 불러오기"),
    ("plugins.create_sample", "Create Sample Plugin", "创建示例插件", "建立範例外掛", "サンプルプラグインを作成", "샘플 플러그인 만들기"),
    ("plugins.reloaded", "Plugins reloaded", "已重新加载插件", "已重新載入外掛", "プラグインを再読み込みしました", "플러그인을 다시 불러옴"),
    ("plugins.created", "Created %s", "已创建 %s", "已建立 %s", "%s を作成しました", "%s 생성됨"),
    ("plugins.disable", "Disable", "停用", "停用", "無効にする", "비활성화"),
    ("plugins.enable", "Enable", "启用", "啟用", "有効にする", "활성화"),
    ("plugins.none", "No plugins", "无插件", "無外掛", "プラグインなし", "플러그인 없음"),
    ("plugins.loaded", "Loaded", "已加载", "已載入", "読み込み済み", "로드됨"),
    ("plugins.disabled", "Disabled", "已停用", "已停用", "無効", "비활성화됨"),
    ("plugins.load_failed", "Cannot load", "无法加载", "無法載入", "読み込めません", "불러올 수 없음"),
    ("plugins.init_failed", "Initialization failed", "初始化失败", "初始化失敗", "初期化に失敗しました", "초기화 실패"),
    ("plugins.missing", "File missing", "文件缺失", "檔案遺失", "ファイルがありません", "파일 없음"),
    ("plugins.script_error", "Script error", "脚本错误", "腳本錯誤", "スクリプトエラー", "스크립트 오류"),
    # settings
    ("settings.title", "Settings", "设置", "設定", "設定", "설정"),
    ("settings.language", "Language", "语言", "語言", "言語", "언어"),
    ("settings.font", "Font", "字体", "字型", "フォント", "글꼴"),
    ("settings.font_pixel", "Pixel", "像素", "像素", "ピクセル", "픽셀"),
    ("settings.font_system", "System", "系统", "系統", "システム", "시스템"),
    ("settings.font_unavailable", "No system font on this platform", "此平台没有可用的系统字体",
     "此平台沒有可用的系統字型", "このプラットフォームではシステムフォントを使えません", "이 플랫폼에는 시스템 글꼴이 없습니다"),
    ("settings.ui_scale", "UI scale", "界面缩放", "介面縮放", "UI の拡大率", "UI 배율"),
    ("settings.close", "Close", "关闭", "關閉", "閉じる", "닫기"),
]


def main():
    here = os.path.dirname(os.path.abspath(__file__))
    out = os.path.join(here, "..", "resources", "lang")
    os.makedirs(out, exist_ok=True)
    seen = set()
    for row in ROWS:
        assert len(row) == 1 + len(LANGS), row[0]
        assert row[0] not in seen, "duplicate key " + row[0]
        seen.add(row[0])
    for i, lang in enumerate(LANGS):
        table = {row[0]: row[1 + i] for row in ROWS}
        with open(os.path.join(out, "editor.%s.json" % lang), "w", encoding="utf-8") as f:
            json.dump(table, f, ensure_ascii=False, indent=2)
            f.write("\n")
    print("lang_editor: %d keys x %d languages -> engine/resources/lang/" % (len(ROWS), len(LANGS)))


if __name__ == "__main__":
    main()
