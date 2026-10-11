package com.rokid.rc.shared.protocol

/**
 * root 取得狀態代碼（結構化；眼鏡端只送代碼，手機端以**自己語系**組字）。
 *
 * 目的（P0-b 失敗可視化）：讓使用者一眼看出「root 現在能不能用」以及
 * **下一步要跑哪支腳本**，而不是只在日誌裡看到 `root required?`。
 */
object RootStates {
    const val UNKNOWN = "root_unknown"
    /** root 可用（daemon 活著）。 */
    const val OK = "root_ok"
    /** 偵測到失效，正在自動重建中。 */
    const val REARMING = "root_rearming"
    /** 自動重建成功。 */
    const val REARMED = "root_rearmed"
    /** 失效且**無人可幫忙觸發**：需操作者跑腳本（detail = 建議腳本）。 */
    const val LOST_NEED_ARM = "root_lost_need_arm"
    /** 失效但仍在冷卻期（避免猛打 exploit）。 */
    const val LOST_COOLDOWN = "root_lost_cooldown"
}

/**
 * 提權度量（P2）：最近一次與累計的成功／失敗／耗時，隨 `STATUS` 回報手機。
 *
 * @param lastEngine   最近一次使用的引擎（`shizuku`／`bridge`／`direct`／`none`）
 * @param lastOk       最近一次是否成功
 * @param lastMs       最近一次耗時（毫秒）
 * @param attempts     累計嘗試次數
 * @param successes    累計成功次數
 * @param firstTryOk   其中「第一次嘗試就成功」的次數
 * @param keeperChecks daemon 看守檢查次數（P0）
 * @param keeperRearms daemon 自動重建次數（P0）
 */
data class RootMetrics(
    val lastEngine: String = "none",
    val lastOk: Boolean = false,
    val lastMs: Long = 0L,
    val attempts: Int = 0,
    val successes: Int = 0,
    val firstTryOk: Int = 0,
    val keeperChecks: Int = 0,
    val keeperRearms: Int = 0
)
