package com.rokid.rc.shared.protocol

/**
 * 相機端錯誤碼（眼鏡 → 手機的**機器可讀**原因）。
 *
 * 這些字串經由 `CommandRouter.ackError()` 或戒指 `RingOutcome(..., arg)` 送到手機，
 * 由手機以**自己的語系**組字（見 phone `ui/RingText.kt` 的 `errorDetail()`）。
 *
 * ⚠ 改這裡等於改協定：兩端必須一起更新。
 *
 * 背景（2026-09-13）：原本所有失敗都回 `"camera not ready"`，導致「**正在錄影**」與
 * 「**服務沒起來**」在使用者眼中長得一樣，把相機狀態問題誤導成「戒指連線失敗」。
 * 見 `reports/aivela_ring_connection_failure_2026-09-13.md`。
 */
object CameraErrors {

    /** 相機尚未就緒（服務剛啟動、bind 失敗、Activity 未在前景…）。 */
    const val NOT_READY = "camera_not_ready"

    /** **正在錄影**：本專案採單一 use case（拍照與錄影互斥），錄影中無法拍照。 */
    const val BUSY_RECORDING = "busy_recording"

    /** 前一次拍照仍在進行，或距上次拍照不足最小間隔（連拍保護）。 */
    const val BUSY = "busy"

    /** 錄影啟動逾時（等不到 `VideoRecordEvent.Start`）→ 已自動停止並切回拍照模式。 */
    const val RECORD_START_TIMEOUT = "record_start_timeout"
}
