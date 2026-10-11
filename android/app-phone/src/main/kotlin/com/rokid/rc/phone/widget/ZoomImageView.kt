package com.rokid.rc.phone.widget

import android.content.Context
import android.graphics.Matrix
import android.graphics.RectF
import android.util.AttributeSet
import android.view.GestureDetector
import android.view.MotionEvent
import android.view.ScaleGestureDetector
import androidx.appcompat.widget.AppCompatImageView

/** Photo viewer with pinch-to-zoom, double-tap zoom/restore, and drag-to-pan. */
class ZoomImageView @JvmOverloads constructor(
    context: Context,
    attrs: AttributeSet? = null
) : AppCompatImageView(context, attrs) {

    private val matrix = Matrix()
    private val detector = ScaleGestureDetector(context, ScaleListener())
    private val flingDetector = GestureDetector(context, TapListener())

    private var minScale = 1f
    private var maxScale = 5f
    private var lastX = 0f
    private var lastY = 0f

    init {
        scaleType = ScaleType.MATRIX
    }

    private val baseMatrix = Matrix()

    override fun setImageBitmap(bm: android.graphics.Bitmap?) {
        super.setImageBitmap(bm)
        if (bm != null) {
            fitToScreen()
        }
    }

    fun fitToScreen() {
        val bm = drawable ?: return
        val dw = bm.intrinsicWidth.toFloat()
        val dh = bm.intrinsicHeight.toFloat()
        if (dw <= 0 || dh <= 0) return
        val vw = width.toFloat()
        val vh = height.toFloat()
        if (vw <= 0 || vh <= 0) return
        matrix.reset()
        val scale = minOf(vw / dw, vh / dh, 1f)
        matrix.postScale(scale, scale)
        matrix.postTranslate((vw - dw * scale) / 2, (vh - dh * scale) / 2)
        baseMatrix.set(matrix)
        minScale = scale
        imageMatrix = matrix
    }

    override fun onSizeChanged(w: Int, h: Int, oldw: Int, oldh: Int) {
        super.onSizeChanged(w, h, oldw, oldh)
        fitToScreen()
    }

    override fun onTouchEvent(ev: MotionEvent): Boolean {
        detector.onTouchEvent(ev)
        flingDetector.onTouchEvent(ev)
        when (ev.actionMasked) {
            MotionEvent.ACTION_DOWN -> {
                lastX = ev.x
                lastY = ev.y
            }
            MotionEvent.ACTION_MOVE -> {
                // one-finger drag to pan (requires being zoomed in)
                if (ev.pointerCount == 1 && currentScale() > minScale * 1.05f) {
                    matrix.postTranslate(ev.x - lastX, ev.y - lastY)
                    imageMatrix = matrix
                    lastX = ev.x
                    lastY = ev.y
                }
            }
            MotionEvent.ACTION_UP, MotionEvent.ACTION_CANCEL -> constrain()
        }
        return true
    }

    private inner class ScaleListener : ScaleGestureDetector.SimpleOnScaleGestureListener() {
        override fun onScaleBegin(d: ScaleGestureDetector): Boolean = true

        override fun onScale(d: ScaleGestureDetector): Boolean {
            val f = currentScale()
            val next = (f * d.scaleFactor).coerceIn(minScale, maxScale)
            if (f > minScale || next > f) {
                matrix.postScale(next / f, next / f, d.focusX, d.focusY)
                imageMatrix = matrix
            }
            return true
        }
    }

    private inner class TapListener : GestureDetector.SimpleOnGestureListener() {
        override fun onDoubleTap(e: MotionEvent): Boolean {
            val cur = currentScale()
            if (cur > minScale * 1.1f) {
                matrix.set(baseMatrix)
            } else {
                matrix.postScale(2.2f / cur, 2.2f / cur, e.x, e.y)
            }
            constrain()
            imageMatrix = matrix
            return true
        }

        override fun onSingleTapConfirmed(e: MotionEvent): Boolean {
            onSingleTap?.invoke()
            return true
        }

        override fun onDown(e: MotionEvent): Boolean = true
    }

    /** Single tap (lets the outside toggle the chrome). */
    var onSingleTap: (() -> Unit)? = null

    private fun currentScale(): Float {
        val v = FloatArray(9)
        matrix.getValues(v)
        return v[Matrix.MSCALE_X]
    }

    /** Prevent dragging too far out of bounds. */
    private fun constrain() {
        val bm = drawable ?: return
        val dw = bm.intrinsicWidth.toFloat()
        val dh = bm.intrinsicHeight.toFloat()
        val vw = width.toFloat()
        val vh = height.toFloat()
        if (dw <= 0 || dh <= 0) return
        val rect = RectF(0f, 0f, dw, dh)
        matrix.mapRect(rect)
        var dx = 0f
        var dy = 0f
        if (rect.width() < vw) dx = (vw - rect.width()) / 2 - rect.left
        else {
            if (rect.left > 0) dx = -rect.left
            if (rect.right < vw) dx = vw - rect.right
        }
        if (rect.height() < vh) dy = (vh - rect.height()) / 2 - rect.top
        else {
            if (rect.top > 0) dy = -rect.top
            if (rect.bottom < vh) dy = vh - rect.bottom
        }
        if (dx != 0f || dy != 0f) {
            matrix.postTranslate(dx, dy)
            imageMatrix = matrix
        }
    }
}
