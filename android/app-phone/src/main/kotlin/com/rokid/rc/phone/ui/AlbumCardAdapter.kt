package com.rokid.rc.phone.ui

import android.graphics.Bitmap
import android.view.LayoutInflater
import android.view.View
import android.view.ViewGroup
import android.widget.ImageView
import android.widget.TextView
import androidx.recyclerview.widget.RecyclerView
import com.rokid.rc.phone.R
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.Job
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.cancel
import kotlinx.coroutines.launch

/** 相簿卡片（2 欄方形封面）。 */
data class AlbumCard(
    val id: String,
    val name: String,
    val countText: String,
    val subText: String? = null,
    val empty: Boolean = false,
    /** 徽章數字（項數）；null 表示不顯示徽章。 */
    val badgeCount: Int? = null
)

/**
 * 相簿卡 adapter（相簿頁與本機頁共用）。
 *
 * 支援**多選模式**：長按進入選取、點擊切換勾選；進入/離開與選取變化會回呼
 * [onSelectionChanged]，由畫面決定工具列與批次動作。
 */
class AlbumCardAdapter(
    private var items: List<AlbumCard> = emptyList(),
    private val coverLoader: suspend (AlbumCard) -> Bitmap? = { null },
    private val onClick: (AlbumCard) -> Unit = {},
    private val onLongClick: (AlbumCard) -> Unit = {},
    /** true（本機頁）＝長按進入多選；false（相簿頁）＝長按交給 [onLongClick] 開選單。 */
    private val longPressEntersSelection: Boolean = true
) : RecyclerView.Adapter<AlbumCardAdapter.VH>() {

    private val scope = CoroutineScope(SupervisorJob() + Dispatchers.Main.immediate)

    /** 多選狀態。 */
    var selectionMode: Boolean = false
        private set
    val selected = LinkedHashSet<String>()

    /** 選取或模式變化時回呼（用於更新工具列／批次列）。 */
    var onSelectionChanged: (() -> Unit)? = null

    class VH(v: View) : RecyclerView.ViewHolder(v) {
        val cover: ImageView = v.findViewById(R.id.ivCover)
        val name: TextView = v.findViewById(R.id.tvName)
        val count: TextView = v.findViewById(R.id.tvCount)
        val emptyIcon: View = v.findViewById(R.id.ivEmpty)
        val badge: TextView = v.findViewById(R.id.tvBadge)
        val check: ImageView = v.findViewById(R.id.ivCheck)
        val coverBg: View = v.findViewById(R.id.vCoverBg)
    }

    override fun onCreateViewHolder(parent: ViewGroup, viewType: Int): VH =
        VH(LayoutInflater.from(parent.context).inflate(R.layout.item_album_card, parent, false))

    override fun getItemCount(): Int = items.size

    override fun onBindViewHolder(h: VH, position: Int) {
        val a = items[position]
        h.name.text = a.name
        h.count.text = a.countText
        h.cover.setImageDrawable(null)
        val showPlaceholder = a.empty
        h.emptyIcon.visibility = if (showPlaceholder) View.VISIBLE else View.INVISIBLE
        h.cover.visibility = if (showPlaceholder) View.INVISIBLE else View.VISIBLE

        // 項數徽章
        if (a.badgeCount != null && a.badgeCount > 0) {
            h.badge.visibility = View.VISIBLE
            h.badge.text = a.badgeCount.toString()
        } else {
            h.badge.visibility = View.GONE
        }

        // 選取狀態
        val checked = selected.contains(a.id)
        h.check.visibility = if (selectionMode) View.VISIBLE else View.GONE
        h.check.setImageResource(
            if (checked) R.drawable.ic_check_circle_filled else R.drawable.ic_check_circle_empty
        )
        h.itemView.alpha = if (selectionMode && !checked) 0.6f else 1f

        if (!showPlaceholder) {
            val job = Job()
            h.itemView.setTag(R.id.cover_job, job)
            scope.launch(job) {
                val bmp = coverLoader(a)
                if (bmp != null && h.itemView.getTag(R.id.cover_job) === job) {
                    h.cover.setImageBitmap(bmp)
                }
            }
        }

        h.itemView.setOnClickListener {
            if (selectionMode) toggle(a.id) else onClick(a)
        }
        h.itemView.setOnLongClickListener {
            when {
                selectionMode -> toggle(a.id)
                longPressEntersSelection -> enterSelection(a.id)
                else -> onLongClick(a)
            }
            true
        }
    }

    override fun onViewRecycled(h: VH) {
        super.onViewRecycled(h)
        (h.itemView.getTag(R.id.cover_job) as? Job)?.cancel()
        h.itemView.setTag(R.id.cover_job, null)
    }

    // ---------------- 多選 ----------------

    fun enterSelection(id: String) {
        selectionMode = true
        selected.clear()
        selected.add(id)
        notifyDataSetChanged()
        onSelectionChanged?.invoke()
    }

    fun toggle(id: String) {
        if (!selected.remove(id)) selected.add(id)
        if (selected.isEmpty()) {
            selectionMode = false
        }
        notifyDataSetChanged()
        onSelectionChanged?.invoke()
    }

    fun exitSelection() {
        selectionMode = false
        selected.clear()
        notifyDataSetChanged()
        onSelectionChanged?.invoke()
    }

    fun selectAll() {
        selected.clear()
        items.forEach { selected.add(it.id) }
        selectionMode = true
        notifyDataSetChanged()
        onSelectionChanged?.invoke()
    }

    fun submit(list: List<AlbumCard>) {
        items = list
        // 清掉已不存在的選取項目
        val ids = list.map { it.id }.toSet()
        selected.retainAll(ids)
        if (selected.isEmpty()) selectionMode = false
        notifyDataSetChanged()
        onSelectionChanged?.invoke()
    }

    fun release() {
        scope.cancel()
    }
}
