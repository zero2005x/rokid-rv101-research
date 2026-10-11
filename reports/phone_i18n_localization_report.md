# 手機端多國語言（i18n）全在地化與語言選擇按鈕 — 實作與驗證報告

- 日期：2026-09-11
- 範圍：`android/app-phone`（手機端 App）＋ `scripts/i18n/`（翻譯工作流）
- 對應需求：手機端 App 內建「語言選擇」按鈕；介面與操作紀錄不再出現中文殘留

## 1. 結論（TL;DR）

| 項目 | 結果 |
|------|------|
| App 內語言選擇按鈕 | ✅ `btnLanguage`（**2026-09-13 起位置改為「設定 → 語言」卡**；遙控分頁只留拍攝相關），開啟 14 語系 + 「跟隨系統」單選對話框 |
| 切換立即生效 | ✅ 選取後寫入 SharedPreferences → `requireActivity().recreate()`，免重啟 App |
| 重啟後保留 | ✅ `shared_prefs/app_locale.xml` → `LocaleHelper.apply()`（App 啟動時套用） |
| 全在地化 | ✅ 盤點 86 條寫死中文字串（10 條為開發者訊息／語言母語名稱，刻意保留），其餘整理為 68 個字串資源，補齊 14 語系（68 × 14 = 952 筆翻譯）<br>**2026-09-13 現況**：管線 `scripts/i18n/phone_keys.tsv` 已累計 **193 個 key**（＋早期遷移的 91 個 key），14 語系皆 100% 覆蓋（`values-*/strings.xml` 各 284 條） |
| 建置／Lint | ✅ `:app-phone:assembleDebug`、`:app-phone:lintDebug` 皆通過（0 error） |
| 實機驗證 | ✅ en / ja / ar（RTL）/ th 切換、持久化、廣播回歸、操作紀錄在地化全部實測通過 |

## 2. 語言選擇按鈕

- 位置：**「設定」分頁 → 語言卡**（2026-09-13 起；遙控分頁已無「進階」區塊）。
- 標籤：`語言：<目前語言>`（`R.string.btn_language` 為帶 `%1$s` 的格式字串，各語系自帶分隔符：
  `Language: %1$s`、`Langue : %1$s`、`言語：%1$s`、`اللغة: %1$s`…）。
- 對話框：`AlertDialog.setSingleChoiceItems`，清單為「跟隨系統」＋ 14 個**母語名稱**
  （`LocaleHelper.LANGUAGES`：繁體中文／简体中文／English／日本語／한국어／Français／Deutsch／
  Español／Português／Русский／Українська／ไทย／Tiếng Việt／العربية），目前語言預選。
- 選取行為：`LocaleHelper.set(ctx, tag)` → 寫入 `app_locale` → `AppCompatDelegate.setApplicationLocales`
  → `recreate()`；`tag = null`（跟隨系統）會移除該鍵。
- 除錯廣播（既有，未變）：`am broadcast -n com.rokid.rc.phone/.DebugReceiver -a com.rokid.rc.phone.DEBUG --es cmd SET_LOCALE --es lang <tag>`
  （不帶 `--es lang` = 還原跟隨系統）。

## 3. 全在地化：範圍與方法

### 3.1 範圍（86 條 → 68 個 key）

盤點出**非註解**的中文字串 86 條，分佈於 10 個 Kotlin 檔：

| 類別 | 檔案 | 內容 |
|------|------|------|
| 操作紀錄 log（最大宗） | `ui/RemoteFragment.kt` | 拍照／錄影／root／ghostlock／LED／靜音／WiFi／顯示設定／進度／語言切換 |
| Toast／對話框 | `MainActivity.kt`、`MediaActivity.kt`、`ViewerActivity.kt`、`ui/AlbumsFragment.kt`、`ui/LocalFragment.kt` | 藍牙未開、未配對、連線失敗、批次刪除／下載進度與結果、本機相簿刪除 |
| 狀態摘要 | `ui/RemoteFragment.kt` | `相簿 N · 媒體 N · root ✓ · ghostlock 開/關` |
| 媒體型別標籤 | `util/Format.kt` | 影片／照片（改為 `typeLabelRes()` 回傳字串資源 id） |
| WiFi 錯誤訊息 | `Remote.kt` | 見 §3.3（改為型別化例外，不再以字串比對判斷原因） |

**刻意保留中文者**（非 UI，或翻譯反而有害）：

| 位置 | 原因 |
|------|------|
| `LocaleHelper.LANGUAGES` 的母語名稱 | 語言選單必須以**該語言自身**顯示（`日本語`、`العربية`…），不可翻譯 |
| `DebugReceiver.kt` 的 `Log.i(...)` 與 `TEST_LOG` 測試訊息 | logcat／測試工具訊息，僅開發者使用（`TEST_LOG` 為灌入測試資料用） |

### 3.2 工作流（可重建）

```bash
# 1) 字串表（key / zh-TW / en）＝ 唯一來源
scripts/i18n/phone_keys.tsv
> ⚠ 2026-09-13 補充：後續新增的 key（戒指/ghostlock/root/外觀等）由子代理分批翻譯後
> **合併進 `scripts/i18n/out/*.xml`**，`out/` 目錄只保留三個合併檔（`ja_ko_zhCN_vi.xml`、
> `fr_de_es_pt.xml`、`ru_uk_th_ar.xml`）；暫存的 partial 檔合併後即刪除。

# 2) 其餘 12 語系翻譯產出（@@values-xx 區塊；此目錄為翻譯來源保存處）
scripts/i18n/out/{fr_de_es_pt,ja_ko_zhCN_vi,ru_uk_th_ar}.xml
# 3) 術語正規化（對齊各語系既有慣用詞；idempotent）＋品管檢查
python3 scripts/i18n/normalize_locale_terms.py
python3 scripts/i18n/validate_translations.py
# 4) 併入 14 個 strings.xml（可重複執行；含 XML 轉義 & ' < >）
python3 scripts/i18n/apply_phone_strings.py
# 5) 呼叫端替換（一次性，已執行；每個 old 需精確命中指定次數才動手）
python3 scripts/i18n/migrate_phone_call_sites.py
```

翻譯由 3 個子代理平行產出（各 4 語系），產出後以腳本檢查：
key 覆蓋率與順序、`%1$s/%2$s/%3$s/%1$d/%2$d` 佔位符逐一比對、`\n` 保留、
`✓ ✗ ⚠ ●` 前綴保留、技術詞（WiFi/Bluetooth/root/ghostlock/LED/TCP）保持拉丁字母、無未轉義 `& < >`。
`values*/strings.xml` 併入後即為**最終來源**（TSV／out 為翻譯歷史紀錄）。

### 3.3 附帶重構：WiFi 失敗原因型別化

原本 `RemoteFragment` 以**比對例外訊息字串**（`raw.contains("請手動加入")`）判斷失敗原因，
在地化後訊息會隨語系改變而失效；改為 `WifiSetupException(code)`：

| code | 顯示字串 |
|------|----------|
| `AP_TIMEOUT` | `wifi_ap_timeout`（眼鏡 WiFi 熱點啟動逾時…） |
| `AP_NOT_READY` | `wifi_ap_not_ready`（既有） |
| `AP_JOIN_MANUAL` | `wifi_failed_manual`（既有；帶 SSID／密碼） |
| `TCP` | `wifi_tcp_failed`（TCP 連線失敗：…） |

例外本身的 `message` 改為英文除錯字串（僅供 logcat），不再直接顯示給使用者。

## 4. 驗證證據（實機：手機 `<PHONE_SERIAL>`）

| 驗證 | 方法 | 結果 |
|------|------|------|
| 按鈕存在 | `uiautomator dump` | `text="語言：跟隨系統"` |
| 對話框內容 | dump | 標題＋「跟隨系統」＋14 母語名稱＋取消（可捲動） |
| 切 English | 點按清單項 | 介面立即全英文，`app_locale.xml → lang=en` |
| 重啟保留 | `am force-stop` → 重啟 | 仍為 `Albums 1 · Media 4 · root ✓ · ghostlock off` |
| 日文 | 點按清單項 | `アルバム 1 · メディア 5 · root ✓ · ghostlock オフ`、`言語：日本語` |
| 阿拉伯文（RTL） | 點按清單項 + `screencap` | 版面鏡像、`الألبومات ١ · الوسائط ٥ · ghostlock معطّل`（阿拉伯數字） |
| 泰文 | 廣播 SET_LOCALE | `อัลบั้ม 1 · สื่อ 5 · root ✓ · ghostlock ปิด` |
| 操作紀錄在地化 | 拍照／切 LED 後 dump | en：`Taking photo… / ✓ Photo captured: IMG_…jpg`；ar：`مؤشر أخضر مضيء… / ✓ مؤشر أخضر: مضيء` |
| 廣播回歸 | `SET_LOCALE --es lang th` | 寫入 `lang=th` 並於重啟後生效 |
| 還原預設 | `SET_LOCALE`（不帶 lang） | `app_locale.xml` 為空 map，介面跟隨系統（zh-TW） |
| Lint | `:app-phone:lintDebug` | 0 error（過程中修掉 14 個 `StringFormatMatches`：`ap.port` 為 `Int` → `%2$d`） |

截圖：`reports/media/verify_ar_rtl.png`（阿拉伯文 RTL 實機畫面）。

## 5. 來源表審查與術語正規化（第二輪，2026-09-11）

初版上線後回頭審 `scripts/i18n/phone_keys.tsv`（zh-TW／en 來源）並與**各語系既有** `strings.xml`
比對，共修正 3 類問題、150 條字串：

### 5.1 zh-TW／en 來源修正（18 處）

| 類別 | 例子 |
|------|------|
| 繁中半形標點 | `連線失敗: %1$s` → `連線失敗：%1$s`；`快門/錄影` → `快門／錄影` |
| 繁中語順 | `媒體回退藍牙` → `媒體改走藍牙`；LED log `白燈 亮…` → `白燈亮…`（`log_led_switch` 移除多餘空白） |
| 英文語感／既有風格對齊 | `Delete finished: %1$d succeeded` → `Deleted %1$d item(s)`；`Photo failed` → `Photo capture failed`；
`privacy LED` → `privacy indicator`（對齊既有 `sec_led`＝Privacy indicator）；引號改為既有檔使用的 `“%1$s”` |

### 5.2 各語系術語一致性（132 條，新增 `scripts/i18n/normalize_locale_terms.py`）

新字串原本統一寫 `WiFi`／`Bluetooth`／`LED`，但各語系既有檔案早已選定慣用詞，同一檔案內混用會不一致：

| 語系 | WiFi | Bluetooth | 其他 |
|------|------|-----------|------|
| ja | `Wi-Fi` | Bluetooth | `グラス` → **`メガネ`**（對齊 `conn_card_title`） |
| ko | `Wi-Fi` | **`블루투스`** | — |
| de | **`WLAN`** | Bluetooth | — |
| fr / pt / ru / uk / vi / th | `Wi-Fi` | Bluetooth | th 另修掉泰文與外來詞之間的空白 |
| zh-CN | WiFi | **`蓝牙`** | — |
| ar | **`واي فاي`** | **`بلوتوث`** | — |
| zh-TW / en / es | WiFi | Bluetooth | — |

隱私指示燈 5 個 key（`log_led_enabling/disabling/on_ok/off_ok/fail`）與 `led_name` 依各語系
既有 `sec_led`／`led_switch_label` 用詞逐語系改寫（例：ja `プライバシーインジケーター`／`白ランプ`、
de `Datenschutz-Anzeige`／`Weiß-LED`、fr `voyant de confidentialité`、ar `مؤشر الخصوصية`）。
uk 的 silent 用語對齊既有 `silent_switch_label`（`Тиха зйомка`）。

### 5.3 新增品管腳本（可重複執行）

```bash
python3 scripts/i18n/validate_translations.py       # TSV/XML 結構、覆蓋率、佔位符、轉義、術語禁用寫法
python3 scripts/i18n/normalize_locale_terms.py      # 術語正規化（idempotent）
python3 scripts/i18n/apply_phone_strings.py         # 併入 14 個 strings.xml
```

`validate_translations.py` 檢查：68 key 覆蓋率（**首輪數字**；現況 193 key）與順序、`%n$s/%n$d` 佔位符與 en 欄逐一相符、
開頭 `\n` 與 `✓ ✗ ⚠ ●` 保留、無未轉義 `&`、各語系不得出現禁用寫法（如 ja 不得有 `グラス`）。
審查後 `:app-phone:assembleDebug`＋`lintDebug` 仍為 0 error，實機抽驗
zh-TW（`白燈亮…／✓ 白燈：亮`）、en（`Turning privacy indicator off…／✓ Indicator: off`）、
ja（`白ランプ 点灯…／✓ 白ランプ：点灯`）、de（`Weiß-LED an…／✓ Weiß-LED: an`）皆正確。

## 6. 已知限制

1. `DebugReceiver` 的 logcat 與 `TEST_LOG` 測試訊息仍為中文（開發者工具，刻意不譯）。
2. 14 語系翻譯由 AI 產出並以腳本驗證**結構**（佔位符／覆蓋率／轉義），
   未經母語人工校對；若要上架商店建議再請母語者審閱 `scripts/i18n/phone_keys.tsv`。
3. 語言為 **per-app** 設定（不影響系統或其他 App）；`values/` 為繁中預設，
   系統語言若不在 14 語系內會回退繁中。
