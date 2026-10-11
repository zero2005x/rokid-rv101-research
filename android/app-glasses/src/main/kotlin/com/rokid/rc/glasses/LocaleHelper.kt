package com.rokid.rc.glasses

import android.content.Context
import androidx.appcompat.app.AppCompatDelegate
import androidx.core.os.LocaleListCompat

/**
 * Per-app language switching (AppCompatDelegate.setApplicationLocales).
 * When unset it follows the system language; the SET_LOCALE broadcast can override it (stored in SharedPreferences, kept across restarts).
 */
object LocaleHelper {
    private const val PREFS = "app_locale"
    private const val KEY = "lang"

    /** Called from Application.onCreate: apply the saved language. */
    fun apply(context: Context) {
        val lang = context.getSharedPreferences(PREFS, Context.MODE_PRIVATE).getString(KEY, null)
        AppCompatDelegate.setApplicationLocales(
            if (lang.isNullOrEmpty()) LocaleListCompat.getEmptyLocaleList()
            else LocaleListCompat.forLanguageTags(lang)
        )
    }

    /** Set the language (lang=null restores following the system). */
    fun set(context: Context, lang: String?) {
        context.getSharedPreferences(PREFS, Context.MODE_PRIVATE).edit().apply {
            if (lang.isNullOrEmpty()) remove(KEY) else putString(KEY, lang)
        }.apply()
        apply(context)
    }
}
