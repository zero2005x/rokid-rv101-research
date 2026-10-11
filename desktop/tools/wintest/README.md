# wintest — ZeroLightRokid Desktop 的 Windows 測試輔助

## 為什麼是獨立主控台程式，不是 PowerShell 腳本

先前用 PowerShell 直接 P/Invoke，連續踩到四個**假訊號**（每一個都讓我做出錯誤結論）：

| # | 現象 | 原因 |
|---|---|---|
| 1 | 「移動主視窗」變成移動錯的視窗 | `$p.MainWindowHandle` 在行程有第二個 top-level 視窗（紀錄視窗）之後會指向它 |
| 2 | `FindWindowW(cls, $null)` 回傳 0，看起來像視窗不存在 | PowerShell 把 `$null` 封送成空字串 `""`，FindWindow 於是去找「標題為空字串」的視窗 |
| 3 | `GetCapture()` 永遠 NULL，拿它判斷點擊無效 | MSDN：只回報「**呼叫執行緒**」的捕獲視窗 |
| 4 | `Add-Type` 整個型別建不起來 | 預設編譯成執行檔，`public static IntPtr Main()` 被當成進入點候選，警告視為錯誤 |

改成獨立主控台程式後，全部 Win32 呼叫都在 C# 內、字串是 C# 的 `null`、
輸出是純 stdout，可從 bash 直接執行，完全避開上述四類問題。

## 編譯

```bash
CSC="/mnt/c/Windows/Microsoft.NET/Framework64/v4.0.30319/csc.exe"
"$CSC" /nologo /optimize+ /out:ZlTest.exe ZlTest.cs /r:System.Drawing.dll
```

編譯一次即可，之後重複使用（約 11 KB）。

## 指令

| 指令 | 說明 |
|---|---|
| `info` | 視窗狀態（位置、大小、可見、最小化、紀錄視窗是否存在） |
| `logopen` | 紀錄視窗是否已建立（YES / NO） |
| `place` | 把主視窗放回工作區中央（**會先還原**，最小化時才量得到正確尺寸） |
| `front` | 把主視窗帶到前景 |
| `origin` | 客戶區原點在畫面何處（把截圖座標換算成客戶區座標用） |
| `click X Y` | 對主視窗客戶區座標點擊 |
| `esc` | 送 Esc |
| `children main\|log` | 列出子控制項（類別、可見、位置、文字） |
| `childclick ID` | 對紀錄視窗的子控制項送 `BM_CLICK`（真控制項，無自繪命中問題） |
| `shot PATH` | 截取主視窗 |

退出碼：0 成功、1 失敗。

## 已知限制

**`logtext` 無法讀取紀錄視窗的 Rich Edit 內容。**

Rich Edit 的文字無法可靠地用 `WM_GETTEXT` 從另一個行程取得（跨行程只保證
marshalling `WM_USER` 以下的系統訊息，而 Rich Edit 的取文字路徑不走這個）。
主視窗那份 Rich Edit 之所以讀得到，是靠 App 自己另闢的管道。

**要驗證紀錄視窗內容，請用 `shot` 截圖，或請使用者目視確認。**

## 踩過的坑（供後續修改參考）

- `Main` 不可當方法名（見上表 #4）。
- `place` 必須先 `ShowWindow(SW_RESTORE)`：最小化的視窗量到的是 `314x50` 的
  虛擬尺寸，直接 `SetWindowPos` 也不會還原。
- 測試時若視窗被使用者手動移動，`shot` 的截圖會有一半是桌面白底 ——
  這不是程式問題，先跑 `place` 再截圖。
