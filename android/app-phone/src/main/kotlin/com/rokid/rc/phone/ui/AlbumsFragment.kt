package com.rokid.rc.phone.ui

import android.os.Bundle
import android.view.LayoutInflater
import android.view.View
import android.view.ViewGroup
import android.widget.EditText
import android.widget.ImageView
import android.widget.TextView
import androidx.core.content.ContextCompat
import androidx.fragment.app.Fragment
import androidx.lifecycle.lifecycleScope
import androidx.recyclerview.widget.GridLayoutManager
import androidx.recyclerview.widget.RecyclerView
import com.google.android.material.bottomsheet.BottomSheetDialog
import com.google.android.material.dialog.MaterialAlertDialogBuilder
import com.rokid.rc.phone.MainActivity
import com.rokid.rc.phone.R
import com.rokid.rc.phone.Remote
import com.rokid.rc.phone.util.ThumbLoader
import com.rokid.rc.shared.protocol.Album
import kotlinx.coroutines.Job
import kotlinx.coroutines.launch

/** 相簿分頁：2 欄大封面卡牆 + FAB 新增 + 長按選單。 */
class AlbumsFragment : Fragment() {

    private var rootV: View? = null
    private var allCards: List<AlbumCard> = emptyList()
    private var query: String = ""
    /** 排序：name（預設）／count／newest */
    private var sortBy: String = "name"
    private var adapter: AlbumCardAdapter? = null
    private var reloading = false
    private var reloadJob: Job? = null

    override fun onCreateView(
        inflater: LayoutInflater, container: ViewGroup?, savedInstanceState: Bundle?
    ): View {
        val v = inflater.inflate(R.layout.fragment_albums, container, false)
        rootV = v
        Remote.ensureInit(requireContext().applicationContext)
        val activity = requireActivity() as MainActivity

        val rv = v.findViewById<RecyclerView>(R.id.rvAlbums)
        val cols = 3          // 相簿 3 欄（縮圖密度較高，單本時不會空半邊）
        rv.layoutManager = GridLayoutManager(context, cols)
        adapter = AlbumCardAdapter(
            coverLoader = { card -> loadCover(card) },
            onClick = { card ->
                activity.toast(getString(R.string.toast_opening, card.name))
                MainActivity.openRemoteAlbum(activity, card.id, card.name)
            },
            onLongClick = { card -> showAlbumSheet(activity, card) },
            longPressEntersSelection = false
        )
        rv.adapter = adapter

        v.findViewById<View>(R.id.fabAdd).setOnClickListener { showCreateDialog(activity) }
        val toolbar = v.findViewById<com.google.android.material.appbar.MaterialToolbar>(R.id.toolbarAlbums)
        toolbar.setOnMenuItemClickListener { item ->
            when (item.itemId) {
                R.id.action_refresh -> {
                    refresh(); true
                }
                R.id.action_sort -> {
                    showSortDialog(); true
                }
                else -> false
            }
        }
        (toolbar.menu.findItem(R.id.action_search)?.actionView as? androidx.appcompat.widget.SearchView)
            ?.apply {
                queryHint = getString(R.string.action_search)
                setOnQueryTextListener(object : androidx.appcompat.widget.SearchView.OnQueryTextListener {
                    override fun onQueryTextSubmit(text: String?) = false
                    override fun onQueryTextChange(text: String?): Boolean {
                        // 注意：此處 receiver 是 SearchView，`query` 會解析到它的 getQuery()（val）
                        this@AlbumsFragment.query = text.orEmpty().trim()
                        applyFilter()
                        return true
                    }
                })
            }

        refresh()
        return v
    }

    override fun onResume() {
        super.onResume()
        if (rootV != null && isVisible) refresh()
    }

    override fun onDestroyView() {
        reloadJob?.cancel()
        adapter?.release()
        adapter = null
        rootV = null
        super.onDestroyView()
    }

    fun onConnectionChanged() = refresh()

    fun refresh() {
        val v = rootV ?: return
        if (reloading) return
        reloading = true
        val adapter = adapter ?: return
        reloadJob = viewLifecycleOwner.lifecycleScope.launch {
            if (!Remote.isConnected) {
                showState(v, icon = R.drawable.ic_photo_library, msg = getString(R.string.albums_not_connected), retry = true)
            } else {
                Remote.listAlbums().fold(
                    onSuccess = { albums ->
                        v.findViewById<View>(R.id.llState).visibility = View.GONE
                        allCards = albums.map { a -> cardOf(a) }
                        applyFilter()
                    },
                    onFailure = { e ->
                        showState(v, icon = R.drawable.ic_image, msg = getString(R.string.albums_load_failed) + "\n${e.message}", retry = true)
                    }
                )
            }
            reloading = false
        }
    }

    private fun cardOf(a: Album): AlbumCard = AlbumCard(
        badgeCount = a.mediaCount,
        id = a.id,
        name = a.name,
        countText = getString(R.string.album_item_count, a.mediaCount),
        empty = a.mediaCount == 0
    )

    /** 依查詢字串與排序套用到 adapter（搜尋/排序只影響顯示，不動資料）。 */
    private fun applyFilter() {
        val a = adapter ?: return
        var list = allCards
        if (query.isNotEmpty()) {
            list = list.filter { it.name.contains(query, ignoreCase = true) }
        }
        list = when (sortBy) {
            "count" -> list.sortedByDescending { it.badgeCount ?: 0 }
            "newest" -> list.sortedByDescending { it.id }      // id 為時間序（建立時遞增）
            else -> list.sortedBy { it.name.lowercase() }
        }
        a.submit(list)
    }

    /** 排序選單（名稱／項數／最新）。 */
    private fun showSortDialog() {
        val labels = arrayOf(
            getString(R.string.sort_name),
            getString(R.string.sort_count),
            getString(R.string.sort_newest)
        )
        val keys = arrayOf("name", "count", "newest")
        val checked = keys.indexOf(sortBy).coerceAtLeast(0)
        androidx.appcompat.app.AlertDialog.Builder(requireContext())
            .setTitle(R.string.action_sort)
            .setSingleChoiceItems(labels, checked) { dialog, which ->
                sortBy = keys[which]
                applyFilter()
                dialog.dismiss()
            }
            .setNegativeButton(R.string.action_cancel, null)
            .show()
    }

    /**
     * 封面 = 相簿內最新媒體的縮圖。
     * **優先挑照片**：影片縮圖常是第一格全黑，看起來像「沒有封面」（實測）。
     */
    private suspend fun loadCover(card: AlbumCard): android.graphics.Bitmap? {
        if (card.empty) return null
        val media = Remote.listMedia(card.id).getOrNull() ?: return null
        val photo = media.firstOrNull { it.type == com.rokid.rc.shared.protocol.MediaType.PHOTO }
        val target = photo ?: media.firstOrNull() ?: return null
        return ThumbLoader.remote(target.id, 1024)
    }

    private fun showState(v: View, icon: Int, msg: String, retry: Boolean) {
        // 深色主題下圖示需上色，否則幾乎看不見
        v.findViewById<View>(R.id.llState).visibility = View.VISIBLE
        v.findViewById<ImageView>(R.id.ivStateIcon).setImageDrawable(ContextCompat.getDrawable(requireContext(), icon))
        v.findViewById<TextView>(R.id.tvStateTitle).text = msg
        v.findViewById<View>(R.id.btnStateRetry).visibility = if (retry) View.VISIBLE else View.GONE
        v.findViewById<View>(R.id.btnStateRetry).setOnClickListener { refresh() }
    }

    private fun showCreateDialog(activity: MainActivity) {
        val edit = EditText(activity)
        edit.hint = getString(R.string.hint_album_name)
        MaterialAlertDialogBuilder(activity)
            .setTitle(R.string.dialog_new_album)
            .setView(edit)
            .setPositiveButton(R.string.action_create) { _, _ ->
                val name = edit.text.toString().trim()
                if (name.isEmpty()) return@setPositiveButton
                viewLifecycleOwner.lifecycleScope.launch {
                    Remote.createAlbum(name).fold(
                        onSuccess = {
                            activity.toast(getString(R.string.msg_album_created))
                            refresh()
                        },
                        onFailure = { e -> activity.toast(getString(R.string.toast_create_fail, e.message ?: "")) }
                    )
                }
            }
            .setNegativeButton(R.string.action_cancel, null)
            .show()
    }

    private fun showRenameDialog(activity: MainActivity, albumId: String, current: String) {
        val edit = EditText(activity)
        edit.setText(current)
        MaterialAlertDialogBuilder(activity)
            .setTitle(R.string.dialog_rename_album)
            .setView(edit)
            .setPositiveButton(R.string.action_ok) { _, _ ->
                val name = edit.text.toString().trim()
                if (name.isEmpty()) return@setPositiveButton
                viewLifecycleOwner.lifecycleScope.launch {
                    Remote.renameAlbum(albumId, name).fold(
                        onSuccess = {
                            activity.toast(getString(R.string.msg_album_renamed))
                            refresh()
                        },
                        onFailure = { e -> activity.toast(getString(R.string.toast_rename_fail, e.message ?: "")) }
                    )
                }
            }
            .setNegativeButton(R.string.action_cancel, null)
            .show()
    }

    private fun showAlbumSheet(activity: MainActivity, card: AlbumCard) {
        val sheet = BottomSheetDialog(requireContext())
        val content = layoutInflater.inflate(
            com.rokid.rc.phone.R.layout.sheet_album_menu, null, false
        )
        content.findViewById<TextView>(com.rokid.rc.phone.R.id.sheetTitle).text = card.name
        content.findViewById<View>(com.rokid.rc.phone.R.id.sheetView).setOnClickListener {
            sheet.dismiss()
            MainActivity.openRemoteAlbum(activity, card.id, card.name)
        }
        content.findViewById<View>(com.rokid.rc.phone.R.id.sheetRename).setOnClickListener {
            sheet.dismiss()
            showRenameDialog(activity, card.id, card.name)
        }
        content.findViewById<View>(com.rokid.rc.phone.R.id.sheetDelete).setOnClickListener {
            sheet.dismiss()
            MaterialAlertDialogBuilder(activity)
                .setTitle(R.string.action_delete)
                .setMessage(getString(R.string.confirm_delete_album, card.name))
                .setPositiveButton(R.string.action_delete) { _, _ ->
                    viewLifecycleOwner.lifecycleScope.launch {
                        Remote.deleteAlbum(card.id).fold(
                            onSuccess = {
                                activity.toast(getString(R.string.msg_album_deleted))
                                refresh()
                            },
                            onFailure = { e -> activity.toast(getString(R.string.toast_delete_fail, e.message ?: "")) }
                        )
                    }
                }
                .setNegativeButton(R.string.action_cancel, null)
                .show()
        }
        sheet.setContentView(content)
        sheet.show()
    }
}
