package com.rokid.rc.phone.ui

import android.os.Bundle
import com.rokid.rc.shared.protocol.MediaTypes
import android.view.LayoutInflater
import android.view.View
import android.view.ViewGroup
import android.widget.ImageView
import android.widget.TextView
import androidx.core.content.ContextCompat
import androidx.fragment.app.Fragment
import androidx.recyclerview.widget.GridLayoutManager
import androidx.recyclerview.widget.RecyclerView
import androidx.lifecycle.lifecycleScope
import com.google.android.material.bottomsheet.BottomSheetDialog
import com.google.android.material.dialog.MaterialAlertDialogBuilder
import com.rokid.rc.phone.MainActivity
import com.rokid.rc.phone.R
import com.rokid.rc.phone.Remote
import com.rokid.rc.phone.util.Format
import com.rokid.rc.phone.util.LocalStore
import com.rokid.rc.phone.util.ThumbLoader
import kotlinx.coroutines.Job
import kotlinx.coroutines.launch

/** 本機分頁：顯示已下載到手機的相簿。 */
class LocalFragment : Fragment() {

    private var rootV: View? = null
    private var adapter: AlbumCardAdapter? = null
    private var reloading = false
    private var reloadJob: Job? = null

    override fun onCreateView(
        inflater: LayoutInflater, container: ViewGroup?, savedInstanceState: Bundle?
    ): View {
        val v = inflater.inflate(R.layout.fragment_local, container, false)
        rootV = v
        Remote.ensureInit(requireContext().applicationContext)
        val activity = requireActivity() as MainActivity

        val rv = v.findViewById<RecyclerView>(R.id.rvLocalAlbums)
        rv.layoutManager = GridLayoutManager(context, 3)   // 本機同樣 3 欄
        adapter = AlbumCardAdapter(
            coverLoader = { card -> localCover(card) },
            onClick = { card ->
                MainActivity.openLocalAlbum(activity, card.id, card.name)
            },
            onLongClick = { card -> showLocalSheet(activity, card) }
        ).also { a ->
            a.onSelectionChanged = { updateSelectionBar() }
        }
        rv.adapter = adapter
        v.findViewById<View>(R.id.btnSelAll).setOnClickListener { adapter?.selectAll() }
        v.findViewById<View>(R.id.btnSelCancel).setOnClickListener { adapter?.exitSelection() }
        v.findViewById<View>(R.id.btnSelDelete).setOnClickListener { confirmBatchDelete() }

        v.findViewById<com.google.android.material.appbar.MaterialToolbar>(R.id.toolbarLocal)
            .setOnMenuItemClickListener { item ->
                if (item.itemId == R.id.action_refresh) {
                    refresh()
                    true
                } else false
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
            // 連線時順便把眼鏡端的相簿名稱同步到本機索引
            if (Remote.isConnected) {
                Remote.listAlbums().onSuccess { LocalStore.syncNamesFromRemote(it) }
            }
            val albums = LocalStore.listLocalAlbums()
            updateSummary()
            if (albums.isEmpty()) {
                v.findViewById<View>(R.id.llState).visibility = View.VISIBLE
                v.findViewById<ImageView>(R.id.ivStateIcon)
                    .setImageDrawable(ContextCompat.getDrawable(requireContext(), R.drawable.ic_folder))
                v.findViewById<ImageView>(R.id.ivStateIcon).imageTintList =
                    android.content.res.ColorStateList.valueOf(
                        ContextCompat.getColor(requireContext(), R.color.primary)
                    )
            } else {
                v.findViewById<View>(R.id.llState).visibility = View.GONE
                adapter.submit(
                    albums.map { a ->
                        AlbumCard(
                            id = a.albumId,
                            name = a.name,
                            countText = getString(R.string.album_item_count, a.mediaCount) +
                                " · " + Format.size(a.sizeBytes),
                            empty = false
                        )
                    }
                )
            }
            reloading = false
        }
    }

    /** 摘要列：已下載 N 本 · M 項 · 佔用容量。 */
    private fun updateSummary() {
        val v = rootV ?: return
        val albums = LocalStore.listLocalAlbums()
        val tv = v.findViewById<TextView>(R.id.tvLocalSummary)
        if (albums.isEmpty()) {
            tv.visibility = View.GONE
            return
        }
        val items = albums.sumOf { it.mediaCount }
        val bytes = albums.sumOf { it.sizeBytes }
        tv.text = getString(R.string.local_summary, albums.size, items, Format.size(bytes))
        tv.visibility = View.VISIBLE
    }

    /** 更新批次列（選取數量與按鈕）。 */
    private fun updateSelectionBar() {
        val v = rootV ?: return
        val a = adapter ?: return
        val bar = v.findViewById<View>(R.id.llSelection)
        if (!a.selectionMode) {
            bar.visibility = View.GONE
            return
        }
        bar.visibility = View.VISIBLE
        v.findViewById<TextView>(R.id.tvSelCount).text =
            getString(R.string.selected_albums, a.selected.size)
    }

    /** 批次刪除本機相簿（僅刪手機上的副本）。 */
    private fun confirmBatchDelete() {
        val a = adapter ?: return
        val ids = a.selected.toList()
        if (ids.isEmpty()) return
        com.google.android.material.dialog.MaterialAlertDialogBuilder(requireContext())
            .setTitle(R.string.action_delete)
            .setMessage(getString(R.string.confirm_delete_local_albums, ids.size))
            .setPositiveButton(R.string.action_delete) { _, _ ->
                ids.forEach { LocalStore.albumDir(it).deleteRecursively() }
                a.exitSelection()
                toast(getString(R.string.toast_local_albums_deleted, ids.size))
                refresh()
            }
            .setNegativeButton(R.string.action_cancel, null)
            .show()
    }

    private fun toast(msg: String) {
        (activity as? MainActivity)?.toast(msg)
    }

    private suspend fun localCover(card: AlbumCard): android.graphics.Bitmap? {
        val dir = LocalStore.albumDir(card.id)
        val newest = dir.listFiles()
            ?.filter { it.isFile && !MediaTypes.isPartial(it.name) }
            ?.maxByOrNull { it.lastModified() }
            ?: return null
        if (MediaTypes.isVideo(newest.name)) return null
        return ThumbLoader.local(newest, 1024)
    }

    private fun showLocalSheet(activity: MainActivity, card: AlbumCard) {
        val sheet = BottomSheetDialog(requireContext())
        val content = layoutInflater.inflate(R.layout.sheet_album_menu, null, false)
        content.findViewById<TextView>(R.id.sheetTitle).text = card.name
        content.findViewById<View>(R.id.sheetView).setOnClickListener {
            sheet.dismiss()
            MainActivity.openLocalAlbum(activity, card.id, card.name)
        }
        content.findViewById<View>(R.id.sheetRename).visibility = View.GONE
        content.findViewById<TextView>(R.id.sheetDelete).text = getString(R.string.local_album_delete)
        content.findViewById<View>(R.id.sheetDelete).setOnClickListener {
            sheet.dismiss()
            MaterialAlertDialogBuilder(activity)
                .setTitle(R.string.local_album_delete)
                .setMessage(getString(R.string.local_album_delete_msg, card.name, card.countText))
                .setPositiveButton(R.string.action_delete) { _, _ ->
                    LocalStore.albumDir(card.id).deleteRecursively()
                    activity.toast(getString(R.string.toast_local_album_deleted))
                    refresh()
                }
                .setNegativeButton(R.string.action_cancel, null)
                .show()
        }
        sheet.setContentView(content)
        sheet.show()
    }
}
