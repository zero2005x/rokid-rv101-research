package com.rokid.rc.phone

import android.content.Context
import androidx.appcompat.app.AppCompatDelegate

/**
 * 主題模式：淺色／深色（品牌配色兩套，見 values/colors.xml 與 values-night/colors.xml）。
 *
 * - 預設**深色**（深藍底＋亮青，與 launcher icon／眼鏡儀表板一致）
 * - 使用者在「設定 → 外觀」切換後存 prefs，App 啟動時於 `MainActivity.onCreate`
 *   （setContentView 之前）套用，切換當下以 `recreate()` 立即生效。
 */
object ThemeHelper {

    private const val PREFS = "app_theme"
    private const val KEY_LIGHT = "light_mode"

    /** true = 淺色模式。 */
    fun isLight(context: Context): Boolean =
        context.getSharedPreferences(PREFS, Context.MODE_PRIVATE).getBoolean(KEY_LIGHT, false)

    fun setLight(context: Context, light: Boolean) {
        context.getSharedPreferences(PREFS, Context.MODE_PRIVATE)
            .edit().putBoolean(KEY_LIGHT, light).apply()
        apply(context)
    }

    /** 依 prefs 套用夜間模式（於 Activity 建立前呼叫）。 */
    fun apply(context: Context) {
        AppCompatDelegate.setDefaultNightMode(
            if (isLight(context)) AppCompatDelegate.MODE_NIGHT_NO
            else AppCompatDelegate.MODE_NIGHT_YES
        )
    }
}
