# HITCON ZeroDay 版揭露時間線

> 時區：Asia/Taipei（UTC+8）。未知日期不得推測。

| 日期 | 事件 | 證據／備註 |
|---|---|---|
| 2026-07-09 | CVE-2026-43499 已有公開上游資訊 | 公開 advisory；本案因此不屬未公開 root cause |
| 2026-07-10 | 受測 Rokid firmware `1.22.009-20260710-150201` 日期標記 | Build fingerprint／OTA metadata |
| 2026-08-12 23:07 | 蒐集裝置 identity、security baseline、kernel runtime | 原始 `RV101_Disclosure/02_Device_Information/` |
| 2026-08-22 13:46 | 首次 clean uid 0／SELinux Permissive 成功 run | 私密 raw log v9.59 |
| 2026-08-22 14:25 | v9.60 clean demo 再次成功 | 私密 raw log |
| 2026-08-22 14:32–14:39 | 完整 BEFORE→AFTER 與非破壞性證據 | 權威 frozen raw log；平台版為 A02 衍生資料 |
| 2026-08-22 17:21 | ADB 即時補蒐集嘗試中 transport 中斷 | 未產生新 baseline；見內部紀錄 |
| 2026-08-22 | 建立 HITCON ZeroDay 草稿包 | 本目錄；尚未送出 |
| To be confirmed | 寄送 Rokid 初始 disclosure | 待填日期、Message-ID／ticket |
| To be confirmed | 寄送 HITCON 送件前詢問 | 待填日期與回覆 |
| To be confirmed | HITCON 平台正式提交 | 待填 HZD code |
| To be confirmed | HITCON 審核／組織通知 | 待填狀態與日期 |
| To be confirmed | 私密 PoC 交付 | 只記交付對象、方式、hash，不在此貼內容 |
| To be confirmed | 修復 firmware 發布 | 待填版本與 release date |
| To be confirmed | 研究者複測 | 待填結果與 evidence hash |
| To be confirmed | 協調公開 | 必須與 Rokid／HITCON 確認 |

