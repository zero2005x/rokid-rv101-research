package com.rokid.rc.shared.ring

/**
 * 戒指身分比對字串的**唯一來源**。
 *
 * 背景：手機端（路徑 A）與眼鏡端（路徑 B）原本各自在 11 處寫死 `"RGR"`，
 * 而這個字串是**選裝置的唯一依據**（見 `reports/ring_capture_security_audit_2026-09-13.md` §2 R6）。
 * 散落的失敗模式是**靜默的**：只要有一處不同步，「配對的裝置」與「實際監聽的裝置」
 * 就會不一致，而且只留一行 log。
 *
 * ⚠ **這不是安全邊界**：BLE 廣告名稱由周邊自行宣告，任何裝置都能自稱 `RGR`。
 * 真正的來源綁定需要「配對後持久化的 identity address ＋ 使用者確認」，
 * 目前**尚未實作**（同報告 §3）。
 */
object RingIdentity {

    /** 官方 Rokid 戒指（RGR06 等）的預設廣告名稱比對字串。 */
    const val DEFAULT_NAME_PATTERN = "RGR"

    /** 部分戒指只廣告 `RING`；僅在 [DEFAULT_NAME_PATTERN] 完全找不到時作為退化比對。 */
    const val FALLBACK_NAME_PATTERN = "RING"

    /** 眼鏡內建觸控板（`ROKID,PSOC-TP-R`）的名稱比對字串；除錯／替代來源用。 */
    const val TOUCHPAD_NAME_PATTERN = "TP"

    /**
     * 使用者是否明確指定了比對字串。
     *
     * `null`／空字串代表「由呼叫端決定」——手機端解讀為「取 RSSI 最強的可連線裝置」，
     * 眼鏡端解讀為 [DEFAULT_NAME_PATTERN]。此差異原本靠各處的 `?: "RGR"` 表達。
     */
    fun orDefault(pattern: String?): String =
        if (pattern.isNullOrBlank()) DEFAULT_NAME_PATTERN else pattern
}
