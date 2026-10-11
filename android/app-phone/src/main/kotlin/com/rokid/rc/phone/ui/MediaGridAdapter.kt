package com.rokid.rc.phone.ui

import android.graphics.Bitmap
import android.view.LayoutInflater
import android.view.View
import android.view.ViewGroup
import android.widget.ImageView
import android.widget.TextView
import androidx.core.content.ContextCompat
import androidx.recyclerview.widget.RecyclerView
import com.rokid.rc.phone.R
import com.rokid.rc.phone.util.Format
import com.rokid.rc.shared.protocol.MediaItem
import com.rokid.rc.shared.protocol.MediaType
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.Job
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.cancel
import kotlinx.coroutines.launch

/** 媒體照片牆（3 欄正方格 + 多選勾選 + 影片角標）。 */
class MediaGridAdapter(
    private val coverLoader: suspend (MediaItem) -> Bitmap?,
    private val onClick: (MediaItem) -> Unit,
    private val onLongClick: (MediaItem) -> Unit,
    private val showDuration: Boolean = true
) : RecyclerView.Adapter<MediaGridAdapter.VH>() {

    private var items: List<MediaItem> = emptyList()
    private val scope = CoroutineScope(SupervisorJob() + Dispatchers.Main.immediate)

    /** 目前選取（多選模式）的 mediaId 集合。 */
    val selected = LinkedHashSet<String>()

    var selectionMode: Boolean = false
        private set

    class VH(v: View) : RecyclerView.ViewHolder(v) {
        val img: ImageView = v.findViewById(R.id.ivMedia)
        val dur: TextView = v.findViewById(R.id.tvDur)
        val play: ImageView = v.findViewById(R.id.ivPlay)
        val selCheck: ImageView = v.findViewById(R.id.ivSelCheck)
        val selScrim: View = v.findViewById(R.id.vSelScrim)
    }

    override fun onCreateViewHolder(parent: ViewGroup, viewType: Int): VH =
        VH(LayoutInflater.from(parent.context).inflate(R.layout.item_media_grid, parent, false))

    override fun getItemCount(): Int = items.size

    override fun onBindViewHolder(h: VH, position: Int) {
        val m = items[position]
        val video = m.type == MediaType.VIDEO
        h.dur.visibility = if (video && showDuration && m.durationMs > 0) View.VISIBLE else View.GONE
        h.play.visibility = if (video) View.VISIBLE else View.GONE
        h.dur.text = if (video && m.durationMs > 0) Format.duration(m.durationMs) else ""
        bindSelection(h, m.id)

        val job = Job()
        h.img.setTag(R.id.media_job, job)
        h.img.setImageDrawable(ContextCompat.getDrawable(h.img.context, R.drawable.ic_image))
        scope.launch(job) {
            val bmp = coverLoader(m)
            if (bmp != null && h.img.getTag(R.id.media_job) === job) {
                h.img.setImageBitmap(bmp)
            }
        }
        h.itemView.setOnClickListener {
            if (selectionMode) toggle(m.id) else onClick(m)
        }
        h.itemView.setOnLongClickListener {
            if (!selectionMode) {
                selectionMode = true
                selected.clear()
                selected.add(m.id)
                notifyDataSetChanged()
                onSelectionChanged()
            }
            true
        }
    }

    private fun bindSelection(h: VH, id: String) {
        val on = selected.contains(id)
        h.selCheck.visibility = if (selectionMode) View.VISIBLE else View.GONE
        h.selScrim.visibility = if (selectionMode && on) View.VISIBLE else View.GONE
        h.selCheck.setImageResource(
            if (on) R.drawable.ic_check_circle_filled else R.drawable.ic_check_circle_empty
        )
    }

    private fun toggle(id: String) {
        if (!selected.remove(id)) selected.add(id)
        if (selected.isEmpty()) {
            selectionMode = false
            notifyDataSetChanged()
        } else {
            notifyDataSetChanged()
        }
        onSelectionChanged()
    }

    /** 退出多選並清空。 */
    fun exitSelection() {
        if (!selectionMode) return
        selectionMode = false
        selected.clear()
        notifyDataSetChanged()
        onSelectionChanged()
    }

    fun selectAll() {
        items.forEach { selected.add(it.id) }
        notifyDataSetChanged()
        onSelectionChanged()
    }

    var onSelectionChanged: () -> Unit = {}

    fun setSelectionMode(mode: Boolean) {
        if (mode == selectionMode) return
        selectionMode = mode
        if (!mode) selected.clear()
        notifyDataSetChanged()
        onSelectionChanged()
    }

    override fun onViewRecycled(h: VH) {
        super.onViewRecycled(h)
        (h.img.getTag(R.id.media_job) as? Job)?.cancel()
    }

    fun itemsSnapshot(): List<MediaItem> = items

    fun submit(list: List<MediaItem>) {
        items = list
        selected.removeAll { id -> list.none { it.id == id } }
        if (selected.isEmpty()) selectionMode = false
        notifyDataSetChanged()
    }

    fun release() {
        scope.cancel()
    }
}
