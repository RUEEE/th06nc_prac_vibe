#include "locale.h"
#include "practice_jump.h"

#include <windows.h>

#include <algorithm>

namespace {

using T = LocalizedText;
using D = std::array<T, 5>;

T L(const char* chinese, const char* english, const char* japanese)
{
    return {chinese, english, japanese};
}

D All(T value)
{
    return {value, value, value, value, value};
}

D EnHl(T easyNormal, T hardLunatic)
{
    return {easyNormal, easyNormal, hardLunatic, hardLunatic, hardLunatic};
}

T MidNonspell(int number)
{
    static const T names[] = {
        L("道中一非", "Midboss Nonspell 1", "中ボス 通常1"),
        L("道中二非", "Midboss Nonspell 2", "中ボス 通常2"),
        L("道中三非", "Midboss Nonspell 3", "中ボス 通常3"),
    };
    return names[std::clamp(number, 1, 3) - 1];
}

T BossNonspell(int number)
{
    static const T names[] = {
        L("关底一非", "Boss Nonspell 1", "ボス 通常1"),
        L("关底二非", "Boss Nonspell 2", "ボス 通常2"),
        L("关底三非", "Boss Nonspell 3", "ボス 通常3"),
        L("关底四非", "Boss Nonspell 4", "ボス 通常4"),
        L("关底五非", "Boss Nonspell 5", "ボス 通常5"),
        L("关底六非", "Boss Nonspell 6", "ボス 通常6"),
        L("关底七非", "Boss Nonspell 7", "ボス 通常7"),
        L("关底八非", "Boss Nonspell 8", "ボス 通常8"),
    };
    return names[std::clamp(number, 1, 8) - 1];
}

T RandomSpell(int number)
{
    static const T names[] = {
        L("随机体变化符卡 1", "Randomized Variation Spell 1", "ランダム変化スペル 1"),
        L("随机体变化符卡 2", "Randomized Variation Spell 2", "ランダム変化スペル 2"),
        L("随机体变化符卡 3", "Randomized Variation Spell 3", "ランダム変化スペル 3"),
        L("随机体变化符卡 4", "Randomized Variation Spell 4", "ランダム変化スペル 4"),
        L("随机体变化符卡 5", "Randomized Variation Spell 5", "ランダム変化スペル 5"),
    };
    return names[std::clamp(number, 1, 5) - 1];
}

const std::array<T, 19>& FakeShotSpellNames()
{
    static const std::array<T, 19> names = {
        L("火符「火神之光」", "Fire Sign \"Agni Shine\"", "火符「アグニシャイン」"),
        L("火符「火神之光 上级」", "Fire Sign \"Agni Shine High Level\"", "火符「アグニシャイン上級」"),
        L("火符「火神之光辉」", "Fire Sign \"Agni Radiance\"", "火符「アグニレイディアンス」"),
        L("木符「风灵角笛」", "Wood Sign \"Sylphy Horn\"", "木符「シルフィホルン」"),
        L("木符「风灵角笛 上级」", "Wood Sign \"Sylphy Horn High Level\"", "木符「シルフィホルン上級」"),
        L("木符「翠绿风暴」", "Wood Sign \"Green Storm\"", "木符「グリーンストーム」"),
        L("土符「慵懒三石塔」", "Earth Sign \"Lazy Trilithon\"", "土符「レイジィトリリトン」"),
        L("土符「慵懒三石塔 上级」", "Earth Sign \"Lazy Trilithon High Level\"", "土符「レイジィトリリトン上級」"),
        L("土符「三石塔的震动」", "Earth Sign \"Trilithon Shake\"", "土符「トリリトンシェイク」"),
        L("水符「水精公主」", "Water Sign \"Princess Undine\"", "水符「プリンセスウンディネ」"),
        L("水符「湖葬」", "Water Sign \"Bury In Lake\"", "水符「ベリーインレイク」"),
        L("金符「金属疲劳」", "Metal Sign \"Metal Fatigue\"", "金符「メタルファティーグ」"),
        L("金符「银龙」", "Metal Sign \"Silver Dragon\"", "金符「シルバードラゴン」"),
        L("火&土符「环状熔岩带」", "Fire & Earth Sign \"Lava Cromlech\"", "火＆土符「ラーヴァクロムレク」"),
        L("水&木符「水之精灵」", "Water & Wood Sign \"Water Elf\"", "水＆木符「ウォーターエルフ」"),
        L("木&火符「森林大火」", "Wood & Fire Sign \"Forest Blaze\"", "木＆火符「フォレストブレイズ」"),
        L("土&金符「翡翠巨石」", "Earth & Metal Sign \"Emerald Megalith\"", "土＆金符「エメラルドメガリス」"),
        L("金&水符「水银之毒」", "Metal & Water Sign \"Mercury Poison\"", "金＆水符「マーキュリポイズン」"),
        L("随机体变化符卡", "Randomized Variation Spell", "ランダム変化スペル"),
    };
    return names;
}

} // namespace

Locale& Locale::Instance()
{
    static Locale locale;
    return locale;
}

Locale::Locale()
{
    switch (GetACP()) {
    case 932: language_ = Language::Japanese; break;
    case 936:
    case 950:
    case 54936: language_ = Language::Chinese; break;
    default: language_ = Language::English; break;
    }

    text_ = {
        {LocaleText::Language, L("语言", "Language", "言語")},
        {LocaleText::Version, L("版本：%s", "Version: %s", "バージョン：%s")},
        {LocaleText::Licenses, L("许可证与第三方声明", "Licenses and third-party notices", "ライセンスとサードパーティ表記")},
        {LocaleText::BaseTitle, L("th06nc 练习器 - F9–F12 关闭###practice-base", "th06nc Practice - F9–F12 to close###practice-base", "th06nc 練習 - F9–F12で閉じる###practice-base")},
        {LocaleText::InjectionActive, L("64 位注入与 ImGui 已启用。", "64-bit injection and ImGui are active.", "64ビット注入とImGuiが有効です。")},
        {LocaleText::Renderer, L("渲染器：%s", "Renderer: %s", "レンダラー：%s")},
        {LocaleText::ProcessId, L("进程 ID：%lu", "Process ID: %lu", "プロセス ID：%lu")},
        {LocaleText::MenuHotkeyHint, L("F9–F12：打开 / 关闭全屏面板", "F9–F12: open / close this full-screen panel", "F9–F12：全画面パネルを開く / 閉じる")},
        {LocaleText::ShowDemo, L("显示 ImGui 演示窗口", "Show ImGui demo", "ImGuiデモを表示")},
        {LocaleText::AutoShoot, L("自动射击（Shift+D 开启）", "Auto shoot (Shift+D to enable)", "自動ショット（Shift+Dで有効化）")},
        {LocaleText::KeyBindings, L("键位设置", "Key bindings", "キー設定")},
        {LocaleText::KeyUp, L("上", "Up", "上")},
        {LocaleText::KeyDown, L("下", "Down", "下")},
        {LocaleText::KeyLeft, L("左", "Left", "左")},
        {LocaleText::KeyRight, L("右", "Right", "右")},
        {LocaleText::KeySlow, L("低速", "Focus", "低速")},
        {LocaleText::KeyShoot, L("射击", "Shoot", "ショット")},
        {LocaleText::KeyBomb, L("丢雷", "Bomb", "ボム")},
        {LocaleText::ReimuA, L("梦A", "Reimu A", "霊夢A")},
        {LocaleText::ReimuB, L("梦B", "Reimu B", "霊夢B")},
        {LocaleText::MarisaA, L("魔A", "MarisaA", "魔理沙A")},
        {LocaleText::MarisaB, L("魔B", "MarisaB", "魔理沙B")},
        {LocaleText::KeySkip, L("跳过", "Skip", "スキップ")},
        {LocaleText::KeyAutoShoot, L("自动射击切换", "Toggle auto shoot", "自動ショット切替")},
        {LocaleText::KeyRetry, L("重试", "Retry", "リトライ")},
        {LocaleText::KeyExit, L("退出", "Exit", "終了")},
        {LocaleText::KeyConfirm, L("确认", "Confirm", "決定")},
        {LocaleText::ArrowKeyPreset, L("设定成上下左右按键", "Use arrow-key preset", "矢印キープリセット")},
        {LocaleText::WasdKeyPreset, L("设定成 WASD 按键", "Use WASD preset", "WASDプリセット")},
        {LocaleText::SocdMode, L("SOCD", "SOCD", "SOCD")},
        {LocaleText::SocdNone, L("不进行 SOCD（游戏原始处理）", "No SOCD (game handling)", "SOCDなし（ゲーム処理）")},
        {LocaleText::SocdLastInput, L("后覆盖", "Last input wins", "後入力優先")},
        {LocaleText::SocdFirstInput, L("前覆盖", "First input wins", "先入力優先")},
        {LocaleText::SocdNeutral, L("回中", "Neutral", "ニュートラル")},
        {LocaleText::CurrentKey, L("当前：%s", "Current: %s", "現在：%s")},
        {LocaleText::ChooseKey, L("选择按键", "Choose key", "キーを選択")},
        {LocaleText::PressAKey, L("请点击按键…", "Press a key...", "キーを押してください…")},
        {LocaleText::ShowHitboxes, L("显示判定点（仅练习模式）", "Show hitboxes (Practice only)", "当たり判定表示（練習のみ）")},
        {LocaleText::SquareHitboxes, L("将判定设置为方判", "Use square collision", "矩形判定に変更")},
        {LocaleText::HitboxOffset, L("判定偏移", "Hitbox offset", "判定オフセット")},
        {LocaleText::HitboxScale, L("判定缩放", "Hitbox scale", "判定スケール")},
        {LocaleText::HitboxColor, L("判定颜色", "Hitbox color", "判定色")},
        {LocaleText::ApplyStretch, L("应用拉伸模式", "Apply stretch mode", "引き伸ばしモードを適用")},
        {LocaleText::StretchHelp, L("以右侧为锚点进行 4:3 水平放大；练习器 UI 不拉伸。", "Horizontal 4:3 zoom, anchored to the right edge; overlay UI is unchanged.", "右端を基準に4:3で水平拡大します。練習UIは変形しません。")},
        {LocaleText::GameSpeed, L("游戏速度", "Game speed", "ゲーム速度")},
        {LocaleText::Ok, L("确定", "OK", "決定")},
        {LocaleText::Invincible, L("[F1] 无敌", "[F1] Invincible", "[F1] 無敵")},
        {LocaleText::LockLives, L("[F2] 锁残", "[F2] Lock lives", "[F2] 残機固定")},
        {LocaleText::LockBombs, L("[F3] 锁Bomb", "[F3] Lock bombs", "[F3] ボム固定")},
        {LocaleText::LockPower, L("[F4] 锁Power", "[F4] Lock power", "[F4] パワー固定")},
        {LocaleText::LockTime, L("[F5] 锁时", "[F5] Lock time", "[F5] 時間固定")},
        {LocaleText::AutoBomb, L("[F6] 自动Bomb", "[F6] Auto bomb", "[F6] オートボム")},
        {LocaleText::EverlastingBgm, L("[F7] 永续BGM", "[F7] Persistent BGM", "[F7] BGM継続")},
        {LocaleText::DisableBomb, L("[F8] 禁止丢B", "[F8] Disable bomb", "[F8] ボム禁止")},
        {LocaleText::PatchUnsupported, L("当前游戏版本不支持部分补丁", "Some patches are unsupported by this executable", "この実行ファイルでは一部のパッチを使用できません")},
        {LocaleText::PauseMenu, L("练习暂停", "Practice Pause", "練習ポーズ")},
        {LocaleText::Resume, L("继续游戏", "Resume", "再開")},
        {LocaleText::Restart, L("重新开始", "Restart", "リスタート")},
        {LocaleText::SaveReplayAndExit, L("保存录像并退出", "Save replay and exit", "リプレイを保存して終了")},
        {LocaleText::ExitWithoutReplay, L("直接退出", "Exit without replay", "リプレイを保存せず終了")},
        {LocaleText::ReplaySaveHint, L("退出后将进入游戏原生录像保存确认。", "After exiting, the native replay-save confirmation will open.", "終了後、ゲーム標準のリプレイ保存確認が開きます。")},
        {LocaleText::ReplayFileHint, L("练习参数保存在同一个 .rpy 文件中。", "Practice parameters are stored in the same .rpy file.", "練習パラメータは同じ.rpyファイルに保存されます。")},
        {LocaleText::ReplayHook, L("练习录像 Hook：%s", "Practice replay hook: %s", "練習リプレイHook：%s")},
        {LocaleText::PracticeSetup, L("练习设置", "Practice Setup", "練習設定")},
        {LocaleText::Mode, L("模式", "Mode", "モード")},
        {LocaleText::Original, L("原版练习", "Original", "通常練習")},
        {LocaleText::Enhanced, L("增强练习", "Enhanced", "拡張練習")},
        {LocaleText::Stage, L("面数", "Stage", "ステージ")},
        {LocaleText::Stage1, L("第一面", "Stage 1", "ステージ1")},
        {LocaleText::Stage2, L("第二面", "Stage 2", "ステージ2")},
        {LocaleText::Stage3, L("第三面", "Stage 3", "ステージ3")},
        {LocaleText::Stage4, L("第四面", "Stage 4", "ステージ4")},
        {LocaleText::Stage5, L("第五面", "Stage 5", "ステージ5")},
        {LocaleText::Stage6, L("第六面", "Stage 6", "ステージ6")},
        {LocaleText::ExtraStage, L("Extra 面", "Extra Stage", "Extraステージ")},
        {LocaleText::WarpTo, L("跳转到", "Warp to", "移動先")},
        {LocaleText::None, L("无", "None", "なし")},
        {LocaleText::StagePortion, L("道中", "Stage", "道中")},
        {LocaleText::BossMidboss, L("Boss / 道中Boss", "Boss / Midboss", "ボス / 中ボス")},
        {LocaleText::Type, L("类型", "Type", "種類")},
        {LocaleText::Boss, L("Boss", "Boss", "ボス")},
        {LocaleText::Midboss, L("道中Boss", "Midboss", "中ボス")},
        {LocaleText::MidbossNonspell, L("道中Boss 非符", "Midboss Nonspell", "中ボス 通常")},
        {LocaleText::MidbossSpell, L("道中Boss 符卡", "Midboss Spell", "中ボス スペル")},
        {LocaleText::BossNonspell, L("Boss 非符", "Boss Nonspell", "ボス 通常")},
        {LocaleText::BossSpell, L("Boss 符卡", "Boss Spell", "ボス スペル")},
        {LocaleText::Nonspell, L("非符", "Nonspell", "通常")},
        {LocaleText::Spell, L("符卡", "Spell", "スペル")},
        {LocaleText::Frame, L("帧", "Frame", "フレーム")},
        {LocaleText::Chapter, L("章节", "Chapter", "チャプター")},
        {LocaleText::Chapter_1, L("前半", "First Half", "前半")},
        {LocaleText::Chapter_2, L("后半", "Second Half", "後半")},
        {LocaleText::TimelineTime, L("Timeline 时间：%d", "Timeline time: %d", "タイムライン時刻：%d")},
        {LocaleText::NoJump, L("当前面数 / 难度 / 类型无可用跳转", "No jump for this stage/difficulty/type", "このステージ・難易度・種類には移動先がありません")},
        {LocaleText::Jump, L("跳转", "Jump", "移動先")},
        {LocaleText::Dialogue, L("播放对话", "Dialogue", "会話を再生")},
        {LocaleText::Lives, L("残机", "Lives", "残機")},
        {LocaleText::Bombs, L("Bomb", "Bombs", "ボム")},
        {LocaleText::Score, L("分数", "Score", "スコア")},
        {LocaleText::Power, L("Power", "Power", "パワー")},
        {LocaleText::Graze, L("擦弹", "Graze", "グレイズ")},
        {LocaleText::Point, L("蓝点", "Point", "点アイテム")},
        {LocaleText::FakeShot, L("自机伪装", "Fake Shot", "ショット偽装")},
        {LocaleText::Raging495, L("发狂495", "Raging 495", "495年発狂")},
        {LocaleText::DefaultPattern, L("默认", "Default", "デフォルト")},
        {LocaleText::FastPattern, L("快速", "Fast", "高速")},
        {LocaleText::SlowPattern, L("慢速", "Slow", "低速")},
        {LocaleText::Stage4Books, L("四面魔法书设置", "Stage 4 Books", "4面魔導書設定")},
        {LocaleText::Fixed, L("固定", "Fixed", "固定")},
        {LocaleText::MirrorLastThree, L("对称后三本", "Mirror last 3", "後半3冊を対称")},
        {LocaleText::MirrorAll, L("全部对称", "Mirror all", "すべて反転")},
        {LocaleText::RotateBooks, L("轮换", "Rotate", "ローテーション")},
        {LocaleText::RandomizeBookX, L("随机 X", "Random X", "Xをランダム化")},
        {LocaleText::ResetBookY, L("重置 Y", "Reset Y", "Yをリセット")},
        {LocaleText::CopyBookConfig, L("复制配置到剪贴板", "Copy settings", "設定をコピー")},
        {LocaleText::PasteBookConfig, L("从剪贴板粘贴", "Paste settings", "設定を貼り付け")},
        {LocaleText::Start, L("开始", "Start", "開始")},
        {LocaleText::NavigationHelp, L("上下：选择  |  左右：修改  |  确认：开始", "Up/Down: Select  |  Left/Right: Change  |  Confirm: Start", "上下：選択  |  左右：変更  |  決定：開始")},
        {LocaleText::JumpHook, L("练习跳转 Hook：%s", "Practice jump hook: %s", "練習移動Hook：%s")},
        {LocaleText::WarpHelp, L("跳转已启用；资源与特殊设置字段仅用于增强练习。", "Warp is active; resource and special-setting fields apply to Enhanced mode only.", "移動機能は有効です。リソースと特殊設定は拡張練習にのみ適用されます。")},
        {LocaleText::StatusActive, L("已启用", "active", "有効")},
        {LocaleText::StatusForegroundOnly, L("已启用：仅前台输入", "active: foreground only", "有効：前面ウィンドウのみ")},
        {LocaleText::StatusUnsupportedExecutable, L("已禁用：程序版本或指令不匹配", "disabled: executable/prologue mismatch", "無効：実行ファイルまたは命令が不一致")},
        {LocaleText::StatusAllocationFailed, L("已禁用：跳板内存分配失败", "disabled: relay allocation failed", "無効：リレー用メモリの確保に失敗")},
        {LocaleText::StatusX64AllocationFailed, L("已禁用：x64 跳板内存分配失败", "disabled: x64 relay allocation failed", "無効：x64リレー用メモリの確保に失敗")},
        {LocaleText::StatusPatchFailed, L("已禁用：代码补丁失败", "disabled: code patch failed", "無効：コードパッチに失敗")},
        {LocaleText::StatusNotInstalled, L("未安装", "not installed", "未導入")},
        {LocaleText::StatusPracticePartial, L("部分启用：确认/UI/资源 Hook 不可用", "partial: confirmation/native UI/resource hook unavailable", "一部有効：確認・UI・リソースHookが利用不可")},
        {LocaleText::SpellRateTable, L("符卡收率表", "Spell capture rates", "スペルカード取得率")},
        {LocaleText::SpellId, L("符卡 ID", "Spell ID", "スペル ID")},
        {LocaleText::SpellName, L("名称", "Name", "名称")},
        {LocaleText::Captured, L("收取", "Captured", "取得")},
        {LocaleText::Attempt, L("尝试", "Attempts", "挑戦")},
        {LocaleText::Percent, L("收率", "Percent", "取得率")},
    };

    auto add = [&](JumpEnum key, D names) {
        jumps_.emplace(static_cast<int>(key), names);
    };
    auto same = [&](JumpEnum key, T name) { add(key, All(name)); };

    same(TH06NC_ST1_MID1, MidNonspell(1));
    same(TH06NC_ST1_MID2, L("月符「月光」", "Moon Sign \"Moonlight Ray\"", "月符「ムーンライトレイ」"));
    same(TH06NC_ST1_BOSS1, BossNonspell(1));
    same(TH06NC_ST1_BOSS2, L("夜符「夜雀」", "Night Sign \"Night Bird\"", "夜符「ナイトバード」"));
    same(TH06NC_ST1_BOSS3, BossNonspell(2));
    same(TH06NC_ST1_BOSS4, L("暗符「境界线」", "Darkness Sign \"Demarcation\"", "闇符「ディマーケイション」"));

    same(TH06NC_ST2_MID1, MidNonspell(1));
    same(TH06NC_ST2_BOSS1, BossNonspell(1));
    add(TH06NC_ST2_BOSS2, EnHl(
        L("冰符「冰瀑」", "Ice Sign \"Icicle Fall\"", "氷符「アイシクルフォール」"),
        L("雹符「冰雹暴风」", "Hail Sign \"Hailstorm\"", "雹符「ヘイルストーム」")));
    same(TH06NC_ST2_BOSS3, BossNonspell(2));
    same(TH06NC_ST2_BOSS4, L("冻符「完美冻结」", "Freeze Sign \"Perfect Freeze\"", "凍符「パーフェクトフリーズ」"));
    same(TH06NC_ST2_BOSS5, L("雪符「钻石风暴」", "Snow Sign \"Diamond Blizzard\"", "雪符「ダイアモンドブリザード」"));

    same(TH06NC_ST3_MID1, MidNonspell(1));
    add(TH06NC_ST3_MID2, EnHl(
        L("华符「芳华绚烂」", "Flower Sign \"Gorgeous Sweet Flower\"", "華符「芳華絢爛」"),
        L("华符「卷柏9」", "Flower Sign \"Selaginella 9\"", "華符「セラギネラ９」")));
    same(TH06NC_ST3_BOSS1, BossNonspell(1));
    same(TH06NC_ST3_BOSS2, L("虹符「彩虹的风铃」", "Rainbow Sign \"Colorful Rainbow Wind Chime\"", "虹符「彩虹の風鈴」"));
    same(TH06NC_ST3_BOSS3, BossNonspell(2));
    same(TH06NC_ST3_BOSS4, L("幻符「华想梦葛」", "Illusion Sign \"Imaginary Flower Yumezakura\"", "幻符「華想夢葛」"));
    same(TH06NC_ST3_BOSS5, BossNonspell(3));
    add(TH06NC_ST3_BOSS6, EnHl(
        L("彩符「彩雨」", "Colorful Sign \"Colorful Rain\"", "彩符「彩雨」"),
        L("彩符「彩光乱舞」", "Colorful Sign \"Vivid Chaotic Dance\"", "彩符「彩光乱舞」")));
    same(TH06NC_ST3_BOSS7, L("彩符「极彩台风」", "Colorful Sign \"Dazzling Color Typhoon\"", "彩符「極彩颱風」"));

    same(TH06NC_ST4_BOOKS, L("大书库", "Books", "魔導書"));
    same(TH06NC_ST4_MID1, MidNonspell(1));
    same(TH06NC_ST4_BOSS1, BossNonspell(1));
    same(TH06NC_ST4_BOSS2, RandomSpell(1));
    same(TH06NC_ST4_BOSS3, BossNonspell(2));
    same(TH06NC_ST4_BOSS4, RandomSpell(2));
    same(TH06NC_ST4_BOSS5, RandomSpell(3));
    same(TH06NC_ST4_BOSS6, RandomSpell(4));
    same(TH06NC_ST4_BOSS7, RandomSpell(5));

    same(TH06NC_ST5_MID1, MidNonspell(1));
    add(TH06NC_ST5_MID2, EnHl(
        L("奇术「误导」", "Conjuring \"Misdirection\"", "奇術「ミスディレクション」"),
        L("奇术「幻惑误导」", "Conjuring \"Mesmerizing Misdirection\"", "奇術「幻惑ミスディレクション」")));
    same(TH06NC_ST5_BOSS1, BossNonspell(1));
    add(TH06NC_ST5_BOSS2, EnHl(
        L("幻在「时钟遗骸」", "Illusion Existence \"Clock Corpse\"", "幻在「クロックコープス」"),
        L("幻幽「迷幻杰克」", "Illusion Phantom \"Jack the Ludo Bile\"", "幻幽「ジャック・ザ・ルドビレ」")));
    same(TH06NC_ST5_BOSS3, BossNonspell(2));
    add(TH06NC_ST5_BOSS4, EnHl(
        L("幻象「月神之钟」", "Illusion Image \"Luna Clock\"", "幻象「ルナクロック」"),
        L("幻世「世界」", "Illusion World \"The World\"", "幻世「ザ・ワールド」")));
    same(TH06NC_ST5_BOSS5, BossNonspell(3));
    add(TH06NC_ST5_BOSS6, EnHl(
        L("女仆秘技「操弄玩偶」", "Maid Secret Skill \"Manipulating Doll\"", "メイド秘技「操りドール」"),
        L("女仆秘技「杀人玩偶」", "Maid Secret Skill \"Killing Doll\"", "メイド秘技「殺人ドール」")));

    same(TH06NC_ST6_MID1, MidNonspell(1));
    same(TH06NC_ST6_MID2, L("奇术「永恒的温柔」", "Conjuring \"Eternal Meek\"", "奇術「エターナルミーク」"));
    same(TH06NC_ST6_BOSS1, BossNonspell(1));
    add(TH06NC_ST6_BOSS2, EnHl(
        L("天罚「大卫之星」", "Heaven's Punishment \"Star of David\"", "天罰「スターオブダビデ」"),
        L("神罚「幼小的恶魔领主」", "God's Punishment \"Young Demon Lord\"", "神罰「幼きデーモンロード」")));
    same(TH06NC_ST6_BOSS3, BossNonspell(2));
    add(TH06NC_ST6_BOSS4, EnHl(
        L("冥符「红色的冥界」", "Nether Sign \"Scarlet Netherworld\"", "冥符「紅色の冥界」"),
        L("狱符「千根针的针山」", "Hell Sign \"Mountain of a Thousand Needles\"", "獄符「千本の針の山」")));
    same(TH06NC_ST6_BOSS5, BossNonspell(3));
    add(TH06NC_ST6_BOSS6, EnHl(
        L("诅咒「Vlad Tepes的诅咒」", "Curse \"Curse of Vlad Tepes\"", "呪詛「ブラド・ツェペシュの呪い」"),
        L("神术「吸血鬼幻想」", "Divine Art \"Vampire Illusion\"", "神術「吸血鬼幻想」")));
    same(TH06NC_ST6_BOSS7, BossNonspell(4));
    add(TH06NC_ST6_BOSS8, EnHl(
        L("红符「深红射击」", "Scarlet Sign \"Scarlet Shoot\"", "紅符「スカーレットシュート」"),
        L("红符「绯红之主」", "Scarlet Sign \"Scarlet Meister\"", "紅符「スカーレットマイスタ」")));
    add(TH06NC_ST6_BOSS9, EnHl(
        L("「Red Magic」", "\"Red Magic\"", "「レッドマジック」"),
        L("「红色的幻想乡」", "\"Scarlet Gensokyo\"", "「紅色の幻想郷」")));

    same(TH06NC_ST7_MID1, L("月符「沉静的月神」", "Moon Sign \"Silent Selene\"", "月符「サイレントセレナ」"));
    same(TH06NC_ST7_MID2, L("日符「皇家烈焰」", "Sun Sign \"Royal Flare\"", "日符「ロイヤルフレア」"));
    same(TH06NC_ST7_MID3, L("火水木金土符「贤者之石」", "Fire Water Wood Metal Earth Sign \"Philosopher's Stone\"", "火水木金土符「賢者の石」"));
    same(TH06NC_ST7_BOSS1, BossNonspell(1));
    same(TH06NC_ST7_BOSS2, L("禁忌「红莓陷阱」", "Taboo \"Cranberry Trap\"", "禁忌「クランベリートラップ」"));
    same(TH06NC_ST7_BOSS3, BossNonspell(2));
    same(TH06NC_ST7_BOSS4, L("禁忌「莱瓦汀」", "Taboo \"Laevateinn\"", "禁忌「レーヴァテイン」"));
    same(TH06NC_ST7_BOSS5, BossNonspell(3));
    same(TH06NC_ST7_BOSS6, L("禁忌「四重存在」", "Taboo \"Four of a Kind\"", "禁忌「フォーオブアカインド」"));
    same(TH06NC_ST7_BOSS7, BossNonspell(4));
    same(TH06NC_ST7_BOSS8, L("禁忌「笼中鸟」", "Taboo \"Kagome, Kagome\"", "禁忌「カゴメカゴメ」"));
    same(TH06NC_ST7_BOSS9, BossNonspell(5));
    same(TH06NC_ST7_BOSS10, L("禁忌「恋之迷宫」", "Taboo \"Maze of Love\"", "禁忌「恋の迷路」"));
    same(TH06NC_ST7_BOSS11, BossNonspell(6));
    same(TH06NC_ST7_BOSS12, L("禁弹「星弧破碎」", "Forbidden Barrage \"Starbow Break\"", "禁弾「スターボウブレイク」"));
    same(TH06NC_ST7_BOSS13, BossNonspell(7));
    same(TH06NC_ST7_BOSS14, L("禁弹「折反射」", "Forbidden Barrage \"Catadioptric\"", "禁弾「カタディオプトリック」"));
    same(TH06NC_ST7_BOSS15, BossNonspell(8));
    same(TH06NC_ST7_BOSS16, L("禁弹「刻着过去的钟表」", "Forbidden Barrage \"Clock that Ticks Away the Past\"", "禁弾「過去を刻む時計」"));
    same(TH06NC_ST7_BOSS17, L("秘弹「之后就一个人都没有了吗？」", "Secret Barrage \"And Then Will There Be None?\"", "秘弾「そして誰もいなくなるか？」"));
    same(TH06NC_ST7_BOSS18, L("QED「495年的波纹」", "Q.E.D. \"Ripples of 495 Years\"", "QED「495年の波紋」"));
    same(TH06NC_ST7_BOSS19, L("LSC 1", "LSC 1", "LSC 1"));
    same(TH06NC_ST7_BOSS20, L("LSC 2", "LSC 2", "LSC 2"));
    same(TH06NC_ST7_BOSS21, L("LSC 3", "LSC 3", "LSC 3"));

    same(TH06NC_BOOKS, L("魔法书", "BOOKS", "BOOKS"));

    const auto& fakeShotNames = FakeShotSpellNames();
    same(TH06NC_FIRE_1,       fakeShotNames[0]);
    same(TH06NC_FIRE_2,       fakeShotNames[1]);
    same(TH06NC_FIRE_3,       fakeShotNames[2]);
    same(TH06NC_WOOD_1,       fakeShotNames[3]);
    same(TH06NC_WOOD_2,       fakeShotNames[4]);
    same(TH06NC_WOOD_3,       fakeShotNames[5]);
    same(TH06NC_EARTH_1,      fakeShotNames[6]);
    same(TH06NC_EARTH_2,      fakeShotNames[7]);
    same(TH06NC_EARTH_3,      fakeShotNames[8]);
    same(TH06NC_WATER_1,      fakeShotNames[9]);
    same(TH06NC_WATER_2,      fakeShotNames[10]);
    same(TH06NC_METAL_1,      fakeShotNames[11]);
    same(TH06NC_METAL_2,      fakeShotNames[12]);
    same(TH06NC_FIRE_EARTH,   fakeShotNames[13]);
    same(TH06NC_WATER_WOOD,   fakeShotNames[14]);
    same(TH06NC_WOOD_FIRE,    fakeShotNames[15]);
    same(TH06NC_EARTH_METAL,  fakeShotNames[16]);
    same(TH06NC_METAL_WATER,  fakeShotNames[17]);

}

Language Locale::GetLanguage() const noexcept
{
    return language_;
}

void Locale::SetLanguage(Language language) noexcept
{
    language_ = language;
}

const char* Locale::Select(const LocalizedText& text) const noexcept
{
    switch (language_) {
    case Language::Chinese: return text.chinese;
    case Language::Japanese: return text.japanese;
    default: return text.english;
    }
}

const char* Locale::Get(LocaleText text) const
{
    const auto found = text_.find(text);
    return found == text_.end() ? "<missing locale>" : Select(found->second);
}

const char* Locale::GetJump(int key, int difficulty) const
{
    const auto found = jumps_.find(key);
    if (found == jumps_.end())
        return "<missing jump locale>";
    return Select(found->second[std::clamp(difficulty, 0, 4)]);
}

const char* Locale::GetJump(int key, int difficulty, int fakeShot) const
{
    if (fakeShot < 1 || fakeShot > 4 ||
        key < TH06NC_ST4_BOSS2 || key > TH06NC_ST4_BOSS7 ||
        key == TH06NC_ST4_BOSS3)
        return GetJump(key, difficulty);

    const auto& fakeShotNames = FakeShotSpellNames();
    const T& fire1 = fakeShotNames[0];
    const T& fire2 = fakeShotNames[1];
    const T& fire3 = fakeShotNames[2];
    const T& wood1 = fakeShotNames[3];
    const T& wood2 = fakeShotNames[4];
    const T& wood3 = fakeShotNames[5];
    const T& earth1 = fakeShotNames[6];
    const T& earth2 = fakeShotNames[7];
    const T& earth3 = fakeShotNames[8];
    const T& water1 = fakeShotNames[9];
    const T& water2 = fakeShotNames[10];
    const T& metal1 = fakeShotNames[11];
    const T& metal2 = fakeShotNames[12];
    const T& fireEarth = fakeShotNames[13];
    const T& waterWood = fakeShotNames[14];
    const T& woodFire = fakeShotNames[15];
    const T& earthMetal = fakeShotNames[16];
    const T& metalWater = fakeShotNames[17];

    const bool easy = difficulty == 0;
    const bool normal = difficulty == 1;
    const bool hardOrLunatic = difficulty == 2 || difficulty == 3;
    const T* selected = nullptr;
    switch (key) {
    case TH06NC_ST4_BOSS2:
        if (normal) {
            const T* names[] = {&fire1, &water1, &wood1, &earth1};
            selected = names[fakeShot - 1];
        } else if (hardOrLunatic) {
            const T* names[] = {&fire2, &water2, &wood2, &earth2};
            selected = names[fakeShot - 1];
        }
        break;
    case TH06NC_ST4_BOSS4:
        if (easy) {
            const T* names[] = {&fire1, &water1, &wood1, &earth1};
            selected = names[fakeShot - 1];
        } else if (normal) {
            const T* names[] = {&earth2, &wood2, &fire2, &metal1};
            selected = names[fakeShot - 1];
        } else if (hardOrLunatic) {
            const T* names[] = {&earth3, &wood3, &fire3, &metal2};
            selected = names[fakeShot - 1];
        }
        break;
    case TH06NC_ST4_BOSS5: {
        const T* names[] = {&fireEarth, &waterWood, &woodFire, &earthMetal};
        selected = names[fakeShot - 1];
        break;
    }
    case TH06NC_ST4_BOSS6:
        if (!easy) {
            const T* names[] = {&metalWater, &metalWater, &earthMetal, &waterWood};
            selected = names[fakeShot - 1];
        }
        break;
    case TH06NC_ST4_BOSS7:
        if (hardOrLunatic) {
            const T* names[] = {&woodFire, &earthMetal, &fireEarth, &metalWater};
            selected = names[fakeShot - 1];
        }
        break;
    default:
        break;
    }
    return selected ? Select(*selected) : GetJump(key, difficulty);
}

void Locale::AppendAllGlyphText(std::string& output) const
{
    const auto append = [&](const LocalizedText& text) {
        output.append(text.chinese).push_back('\n');
        output.append(text.english).push_back('\n');
        output.append(text.japanese).push_back('\n');
    };

    for (const auto& entry : text_)
        append(entry.second);
    for (const auto& entry : jumps_)
        for (const LocalizedText& text : entry.second)
            append(text);
    for (const LocalizedText& text : FakeShotSpellNames())
        append(text);
}
