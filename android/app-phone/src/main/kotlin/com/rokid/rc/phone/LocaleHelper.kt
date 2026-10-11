package com.rokid.rc.phone

import android.content.Context
import androidx.appcompat.app.AppCompatDelegate
import androidx.core.os.LocaleListCompat

/**
 * per-app 語言切換（AppCompatDelegate.setApplicationLocales）。
 * 未設定時跟隨系統語言；SET_LOCALE 指令可覆寫（存 SharedPreferences，重啟仍保留）。
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

    fun current(context: Context): String? =
        context.getSharedPreferences(PREFS, Context.MODE_PRIVATE).getString(KEY, null)

    /**
     * 支援的語言（locale tag → 母語名稱）。清單順序即對話框顯示順序；
     * `null` = 跟隨系統（標籤由 `R.string.lang_system` 提供）。
     * 與 `res/values-xx/strings.xml` 的 14 語系（含預設繁中）一一對應。
     */
    val LANGUAGES: List<Pair<String?, String>> = listOf(
        "zh-TW" to "繁體中文",
        "zh-CN" to "简体中文",
        "en" to "English",
        "ja" to "日本語",
        "ko" to "한국어",
        "fr" to "Français",
        "de" to "Deutsch",
        "es" to "Español",
        "pt" to "Português",
        "ru" to "Русский",
        "uk" to "Українська",
        "th" to "ไทย",
        "vi" to "Tiếng Việt",
        "ar" to "العربية",
    )

    /** 目前選取的索引（0 = 跟隨系統，其餘對應 [LANGUAGES] + 1）。 */
    fun currentIndex(context: Context): Int {
        val cur = current(context)
        if (cur.isNullOrEmpty()) return 0
        val i = LANGUAGES.indexOfFirst { it.first.equals(cur, ignoreCase = true) }
        return if (i >= 0) i + 1 else 0
    }
}
