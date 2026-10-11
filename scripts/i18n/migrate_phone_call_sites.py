#!/usr/bin/env python3
"""One-off migration: replace the phone app's hardcoded Chinese strings with R.string
resources (full i18n localisation).

- Source list: scripts/i18n/phone_keys.tsv (key / zh-TW / en, plus the other 12 locales from the
  translation output)
- This script only does the call-site replacement: every old string must appear the given number
  of times in the file, otherwise it aborts (to avoid unintended edits).
- It finished running in 2026-02; it is kept as a change record (a rerun aborts on a failed
  assert, which is expected behaviour).
"""
import pathlib
import sys

ROOT = pathlib.Path("/home/kali/rokid_root/android/app-phone/src/main/kotlin/com/rokid/rc/phone")

# (file, [(old, new, count), ...])
PLAN = [
    ("ui/RemoteFragment.kt", [
        ('import com.rokid.rc.phone.Remote',
         'import com.rokid.rc.phone.Remote\nimport com.rokid.rc.phone.WifiSetupException', 1),
        ('log("已斷線")', 'log(getString(R.string.log_disconnected))', 1),
        ('log("WiFi 傳輸已關閉，媒體回退藍牙")', 'log(getString(R.string.log_wifi_off))', 1),
        ('log("啟用 WiFi 傳輸…（首次需在系統彈窗確認加入「${Remote.wifiLink.apInfo.ssid}」）")',
         'log(getString(R.string.log_wifi_enabling, Remote.wifiLink.apInfo.ssid))', 1),
        ('log("✓ WiFi 傳輸已啟用（${ap.ip}:${ap.port}）")',
         'log(getString(R.string.log_wifi_enabled, ap.ip, ap.port))', 1),
        ('''                            val raw = e.message ?: "未知錯誤"
                            val msg = if (raw.contains("請手動加入")) {
                                getString(
                                    R.string.wifi_failed_manual,
                                    Remote.wifiLink.apInfo.ssid,
                                    Remote.wifiLink.apInfo.pass
                                )
                            } else if (raw.contains("熱點未就緒")) {
                                getString(R.string.wifi_ap_not_ready)
                            } else raw
                            log("✗ WiFi 傳輸：$msg")''',
         '''                            val ap = Remote.wifiLink.apInfo
                            val code = (e as? WifiSetupException)?.code
                            val msg = when (code) {
                                WifiSetupException.Code.AP_JOIN_MANUAL ->
                                    getString(R.string.wifi_failed_manual, ap.ssid, ap.pass)
                                WifiSetupException.Code.AP_NOT_READY -> getString(R.string.wifi_ap_not_ready)
                                WifiSetupException.Code.AP_TIMEOUT -> getString(R.string.wifi_ap_timeout)
                                WifiSetupException.Code.TCP -> getString(R.string.wifi_tcp_failed, e.message ?: "")
                                else -> e.message ?: getString(R.string.err_unknown)
                            }
                            log(getString(R.string.log_wifi_failed, msg))''', 1),
        ('toast("尚未連線眼鏡")', 'toast(getString(R.string.toast_not_connected))', 2),
        ('log("拍照中…")', 'log(getString(R.string.log_capturing))', 1),
        ('log("✓ 已拍照 ${m.fileName}")', 'log(getString(R.string.log_photo_ok, m.fileName))', 1),
        ('log("✗ 拍照失敗 ${e.message}")', 'log(getString(R.string.log_photo_fail, e.message ?: ""))', 1),
        ('log("● 錄影中")', 'log(getString(R.string.log_recording))', 1),
        ('log("✗ 錄影失敗 ${e.message}")', 'log(getString(R.string.log_record_fail, e.message ?: ""))', 1),
        ('log("✓ 已存影片 ${m.fileName}")', 'log(getString(R.string.log_video_saved, m.fileName))', 1),
        ('log("✗ 停止失敗 ${e.message}")', 'log(getString(R.string.log_stop_fail, e.message ?: ""))', 1),
        ('log("ghostlock 設定失敗 ${e.message}")', 'log(getString(R.string.log_ghost_fail, e.message ?: ""))', 1),
        ('log("root 模式設定失敗 ${e.message}")', 'log(getString(R.string.log_root_mode_fail, e.message ?: ""))', 1),
        ('''                                    checked && applied -> "✓ 靜音拍攝已開啟（快門/錄影提示音已壓制）"
                                    checked -> "⚠ 靜音已設定，但尚未套用（需先取得 root）"
                                    else -> "✓ 靜音拍攝已關閉（提示音已還原）"''',
         '''                                    checked && applied -> getString(R.string.silent_applied)
                                    checked -> getString(R.string.silent_pending)
                                    else -> getString(R.string.silent_disabled)''', 1),
        ('log("✗ 靜音設定失敗 ${e.message}")', 'log(getString(R.string.log_silent_fail, e.message ?: ""))', 1),
        ('log(if (checked) "開啟隱私指示燈…" else "關閉隱私指示燈…")',
         'log(getString(if (checked) R.string.log_led_enabling else R.string.log_led_disabling))', 1),
        ('onSuccess = { log(if (checked) "✓ 指示燈：亮" else "✓ 指示燈：熄") },',
         'onSuccess = { log(getString(if (checked) R.string.log_led_on_ok else R.string.log_led_off_ok)) },', 1),
        ('log("✗ LED 設定失敗 ${e.message}")', 'log(getString(R.string.log_led_fail, e.message ?: ""))', 1),
        ('log("取得 root 中（可能需數分鐘）…")', 'log(getString(R.string.log_root_acquiring))', 1),
        ('onSuccess = { ok -> log(if (ok) "✓ root 已就緒" else "✗ root 取得失敗") },',
         'onSuccess = { ok -> log(getString(if (ok) R.string.log_root_ready else R.string.log_root_failed)) },', 1),
        ('log("✗ root 失敗 ${e.message}")', 'log(getString(R.string.log_root_fail, e.message ?: ""))', 1),
        ('log("查詢裝置資訊…")', 'log(getString(R.string.log_querying))', 1),
        ('log("✗ 查詢失敗 ${e.message}")', 'log(getString(R.string.log_query_fail, e.message ?: ""))', 1),
        ('log("[進度] ${Msg.string(o, "message")}")',
         'log(getString(R.string.log_progress, Msg.string(o, "message")))', 1),
        ('onSuccess = { log("✓ 眼鏡顯示已更新") },', 'onSuccess = { log(getString(R.string.log_display_ok)) },', 1),
        ('onFailure = { e -> log("✗ 顯示設定失敗 ${e.message}") }',
         'onFailure = { e -> log(getString(R.string.log_display_fail, e.message ?: "")) }', 1),
        ('log("${colorLabel(color)}燈 ${if (checked) "亮" else "熄"}…")',
         'log(\n                    getString(\n                        R.string.log_led_switch,\n'
         '                        ledName(color),\n'
         '                        getString(if (checked) R.string.state_on else R.string.state_off)\n'
         '                    )\n                )', 1),
        ('onSuccess = { log("✓ ${colorLabel(color)}燈：${if (checked) "亮" else "熄"}") },',
         'onSuccess = {\n                        log(\n                            getString(\n'
         '                                R.string.log_led_done,\n'
         '                                ledName(color),\n'
         '                                getString(if (checked) R.string.state_on else R.string.state_off)\n'
         '                            )\n                        )\n                    },', 1),
        ('log("✗ ${color}燈失敗 ${e.message}")',
         'log(getString(R.string.log_led_switch_fail, ledName(color), e.message ?: ""))', 1),
        ('''    private fun colorLabel(color: String): String = when (color) {
        "white" -> "白"
        "green" -> "綠"
        "blue" -> "藍"
        else -> "紅"
    }''',
         '''    /** 燈色名稱（在地化）。 */
    private fun colorLabel(color: String): String = getString(
        when (color) {
            "white" -> R.string.chip_white
            "green" -> R.string.chip_green
            "blue" -> R.string.chip_blue
            else -> R.string.chip_red
        }
    )

    /** LED 名稱（燈色 + LED，依語系組字）。 */
    private fun ledName(color: String): String = getString(R.string.led_name, colorLabel(color))''', 1),
        ('val sum = "相簿 ${s.albumCount} · 媒體 ${s.mediaCount} · root ${if (s.rootAcquired) "✓" else "—"} · ghostlock ${if (s.ghostlockEnabled) "開" else "關"}"',
         '''val sum = getString(
                R.string.status_summary,
                s.albumCount,
                s.mediaCount,
                if (s.rootAcquired) "✓" else "—",
                getString(if (s.ghostlockEnabled) R.string.state_enabled else R.string.state_disabled)
            )''', 1),
        ('log("✓ 語言已切換：" + (labels[which]))',
         'log(getString(R.string.log_lang_switched, labels[which]))', 1),
    ]),
    ("MediaActivity.kt", [
        ('else "本機沒有此相簿的檔案"', 'else getString(R.string.toast_no_local_media)', 1),
        ('getString(R.string.confirm_delete_media, "已選 ${ids.size} 項")',
         'getString(R.string.confirm_delete_media, getString(R.string.sel_count, ids.size))', 1),
        ('"刪除本機的 ${ids.size} 個檔案？（眼鏡端不受影響）"',
         'getString(R.string.confirm_delete_local_files, ids.size)', 1),
        ('tvProgress().text = String.format("正在刪除 %d/%d", i + 1, total)',
         'tvProgress().text = getString(R.string.batch_deleting, i + 1, total)', 1),
        ('toast("刪除完成：成功 $ok${if (fail > 0) "、失敗 $fail" else ""}")',
         'toast(\n            if (fail > 0) getString(R.string.toast_delete_done_fail, ok, fail)\n'
         '            else getString(R.string.toast_delete_done, ok)\n        )', 1),
        ('getString(R.string.sel_count, items.size) + "\\n將從眼鏡端下載原檔（藍牙傳輸較慢）。"',
         'getString(R.string.sel_count, items.size) + getString(R.string.dialog_download_note)', 1),
        ('tvProgress().text = String.format("下載中 %d/%d：%s", i + 1, total, item.fileName)',
         'tvProgress().text = getString(R.string.batch_downloading_file, i + 1, total, item.fileName)', 1),
        ('toast("下載完成：成功 $ok${if (fail > 0) "、失敗 $fail" else ""}")',
         'toast(\n            if (fail > 0) getString(R.string.toast_download_done_fail, ok, fail)\n'
         '            else getString(R.string.toast_download_done, ok)\n        )', 1),
    ]),
    ("ViewerActivity.kt", [
        ('showMsg("本機檔案不存在", false, false)',
         'showMsg(getString(R.string.err_local_file_missing), false, false)', 1),
        ('onFailure = { e -> toast("刪除失敗 ${e.message}") }',
         'onFailure = { e -> toast(getString(R.string.toast_delete_fail, e.message ?: "")) }', 1),
        ('val typeLabel = if (type == MediaType.VIDEO) "影片" else "照片"',
         'val typeLabel = getString(Format.typeLabelRes(type))', 1),
        ('showMsg("下載失敗：${e.message}", true, false)',
         'showMsg(getString(R.string.err_download_fail, e.message ?: ""), true, false)', 1),
    ]),
    ("ui/AlbumsFragment.kt", [
        ('activity.toast("開啟 ${card.name}…")', 'activity.toast(getString(R.string.toast_opening, card.name))', 1),
        ('activity.toast("建立失敗 ${e.message}")',
         'activity.toast(getString(R.string.toast_create_fail, e.message ?: ""))', 1),
        ('activity.toast("更名失敗 ${e.message}")',
         'activity.toast(getString(R.string.toast_rename_fail, e.message ?: ""))', 1),
        ('activity.toast("刪除失敗 ${e.message}")',
         'activity.toast(getString(R.string.toast_delete_fail, e.message ?: ""))', 1),
    ]),
    ("ui/LocalFragment.kt", [
        ('content.findViewById<TextView>(R.id.sheetDelete).text = "刪除本機相簿"',
         'content.findViewById<TextView>(R.id.sheetDelete).text = getString(R.string.local_album_delete)', 1),
        ('.setTitle("刪除本機相簿")', '.setTitle(R.string.local_album_delete)', 1),
        ('.setMessage("刪除手機上的「${card.name}」（${card.countText}）？\\n眼鏡端原檔不受影響。")',
         '.setMessage(getString(R.string.local_album_delete_msg, card.name, card.countText))', 1),
        ('activity.toast("本機相簿已刪除")',
         'activity.toast(getString(R.string.toast_local_album_deleted))', 1),
    ]),
    ("MainActivity.kt", [
        ('toast("請先開啟藍牙")', 'toast(getString(R.string.toast_bt_disabled))', 1),
        ('toast("沒有已配對的藍牙設備，請先在系統藍牙設定中配對眼鏡")',
         'toast(getString(R.string.toast_no_paired_device))', 1),
        ('onFailure = { e -> toast("連線失敗: ${e.message}") }',
         'onFailure = { e -> toast(getString(R.string.toast_connect_failed, e.message ?: "")) }', 1),
    ]),
]


def main() -> int:
    changed = 0
    for rel, edits in PLAN:
        p = ROOT / rel
        s = p.read_text(encoding="utf-8")
        for old, new, count in edits:
            got = s.count(old)
            if got != count:
                print(f"FAIL {rel}: expected {count}x, found {got}x -> {old[:70]!r}", file=sys.stderr)
                return 1
            s = s.replace(old, new)
            changed += 1
        p.write_text(s, encoding="utf-8")
        print(f"OK   {rel}: {len(edits)} replacements")
    print(f"total {changed} replacements")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
