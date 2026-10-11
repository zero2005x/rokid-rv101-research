package com.rokid.rc.glasses

import android.content.Context
import androidx.appcompat.app.AppCompatDelegate
import androidx.core.os.LocaleListCompat

/**
 * per-app 語言切換（AppCompatDelegate.setApplicationLocales）。
 * 未設定時跟隨系統語言；SET_LOCALE 廣播可覆寫（存 SharedPreferences，重啟仍保留）。
 */
object LocaleHelper {
    private const val PREFS = "app_locale"
    private const val KEY = "lang"

    /** Application.onCreate 呼叫：套用已儲存的語言。 */
    fun apply(context: Context) {
        val lang = context.getSharedPreferences(PREFS, Context.MODE_PRIVATE).getString(KEY, null)
        AppCompatDelegate.setApplicationLocales(
            if (lang.isNullOrEmpty()) LocaleListCompat.getEmptyLocaleList()
            else LocaleListCompat.forLanguageTags(lang)
        )
    }

    /** 設定語言（lang=null 還原跟隨系統）。 */
    fun set(context: Context, lang: String?) {
        context.getSharedPreferences(PREFS, Context.MODE_PRIVATE).edit().apply {
            if (lang.isNullOrEmpty()) remove(KEY) else putString(KEY, lang)
        }.apply()
        apply(context)
    }
}
