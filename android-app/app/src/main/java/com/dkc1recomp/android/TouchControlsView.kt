
package com.dkc1recomp.android

import android.content.Context
import android.graphics.Canvas
import android.graphics.Color
import android.graphics.Paint
import android.view.MotionEvent
import android.view.View

class TouchControlsView(
    context: Context,
    private val onInputChanged: (Int) -> Unit
) : View(context) {

    companion object {
        const val B = 0x001
        const val Y = 0x002
        const val SELECT = 0x004
        const val START = 0x008
        const val UP = 0x010
        const val DOWN = 0x020
        const val LEFT = 0x040
        const val RIGHT = 0x080
        const val A = 0x100
        const val X = 0x200
        const val L = 0x400
        const val R = 0x800
    }

    private val pointers = mutableMapOf<Int, Int>()

    private val fillPaint = Paint(Paint.ANTI_ALIAS_FLAG).apply {
        color = Color.argb(105, 220, 220, 220)
        style = Paint.Style.FILL
    }

    private val borderPaint = Paint(Paint.ANTI_ALIAS_FLAG).apply {
        color = Color.argb(180, 255, 255, 255)
        style = Paint.Style.STROKE
        strokeWidth = 3f
    }

    private val pressedFillPaint = Paint(Paint.ANTI_ALIAS_FLAG).apply {
        color = Color.argb(225, 255, 214, 64)
        style = Paint.Style.FILL
    }

    private val glowPaint = Paint(Paint.ANTI_ALIAS_FLAG).apply {
        color = Color.argb(110, 255, 214, 64)
        style = Paint.Style.FILL
    }

    private val faintFillPaint = Paint(Paint.ANTI_ALIAS_FLAG).apply {
        color = Color.argb(55, 220, 220, 220)
        style = Paint.Style.FILL
    }

    private val faintBorderPaint = Paint(Paint.ANTI_ALIAS_FLAG).apply {
        color = Color.argb(95, 255, 255, 255)
        style = Paint.Style.STROKE
        strokeWidth = 2f
    }

    private var activeMask = 0

    private val textPaint = Paint(Paint.ANTI_ALIAS_FLAG).apply {
        color = Color.WHITE
        textAlign = Paint.Align.CENTER
        isFakeBoldText = true
    }

    private data class ButtonPosition(
        val x: Float,
        val y: Float,
        val radius: Float,
        val mask: Int,
        val label: String,
        val faint: Boolean = false
    )

    private data class Layout(
        val dpadX: Float,
        val dpadY: Float,
        val dpadRadius: Float,
        val buttons: List<ButtonPosition>
    )

    private fun layout(): Layout? {
        if (width <= 0 || height <= 0) return null

        val w = width.toFloat()
        val h = height.toFloat()

        val margin = h * 0.05f
        val dpadRadius = h * 0.17f
        val jumpRadius = h * 0.095f
        val swapRadius = h * 0.065f
        val smallRadius = h * 0.045f
        val cornerRadius = h * 0.035f

        val dpadX = margin + dpadRadius
        val dpadY = h - margin - dpadRadius

        val jumpX = w - margin - jumpRadius * 1.15f
        val jumpY = h - margin - jumpRadius * 1.05f

        val bottomY = h - margin - smallRadius
        val topY = margin + cornerRadius

        return Layout(
            dpadX = dpadX,
            dpadY = dpadY,
            dpadRadius = dpadRadius,
            buttons = listOf(
                ButtonPosition(jumpX, jumpY, jumpRadius, B, "B"),
                ButtonPosition(jumpX - jumpRadius * 2.0f, jumpY - jumpRadius * 0.9f, jumpRadius, Y, "Y"),
                ButtonPosition(jumpX + jumpRadius * 0.45f, jumpY - jumpRadius * 2.7f, swapRadius, A, "A"),

                ButtonPosition(w / 2f - smallRadius * 2.4f, bottomY, smallRadius, SELECT, "SEL"),
                ButtonPosition(w / 2f + smallRadius * 2.4f, bottomY, smallRadius, START, "START"),

                ButtonPosition(margin + cornerRadius, topY, cornerRadius, L, "L", faint = true),
                ButtonPosition(w - margin - cornerRadius, topY, cornerRadius, R, "R", faint = true)
            )
        )
    }

    override fun onDraw(canvas: Canvas) {
        super.onDraw(canvas)

        val l = layout() ?: return

        canvas.drawCircle(l.dpadX, l.dpadY, l.dpadRadius, fillPaint)
        canvas.drawCircle(l.dpadX, l.dpadY, l.dpadRadius, borderPaint)

        val arm = l.dpadRadius * 0.34f
        val thickness = l.dpadRadius * 0.48f

        canvas.drawRoundRect(
            l.dpadX - thickness / 2,
            l.dpadY - arm * 2,
            l.dpadX + thickness / 2,
            l.dpadY + arm * 2,
            10f, 10f, borderPaint
        )

        canvas.drawRoundRect(
            l.dpadX - arm * 2,
            l.dpadY - thickness / 2,
            l.dpadX + arm * 2,
            l.dpadY + thickness / 2,
            10f, 10f, borderPaint
        )

        val half = thickness / 2
        val reach = arm * 2
        val cx = l.dpadX
        val cy = l.dpadY
        if (activeMask and UP != 0) {
            canvas.drawRoundRect(cx - half, cy - reach, cx + half, cy, 10f, 10f, pressedFillPaint)
        }
        if (activeMask and DOWN != 0) {
            canvas.drawRoundRect(cx - half, cy, cx + half, cy + reach, 10f, 10f, pressedFillPaint)
        }
        if (activeMask and LEFT != 0) {
            canvas.drawRoundRect(cx - reach, cy - half, cx, cy + half, 10f, 10f, pressedFillPaint)
        }
        if (activeMask and RIGHT != 0) {
            canvas.drawRoundRect(cx, cy - half, cx + reach, cy + half, 10f, 10f, pressedFillPaint)
        }

        for (button in l.buttons) {
            drawButton(canvas, button)
        }
    }

    private fun drawButton(canvas: Canvas, button: ButtonPosition) {
        val pressed = (activeMask and button.mask) != 0

        val radius = if (pressed) button.radius * 0.92f else button.radius
        if (pressed) {
            canvas.drawCircle(button.x, button.y, button.radius * 1.14f, glowPaint)
        }

        val faint = button.faint && !pressed
        canvas.drawCircle(
            button.x, button.y, radius,
            when {
                pressed -> pressedFillPaint
                faint -> faintFillPaint
                else -> fillPaint
            }
        )
        canvas.drawCircle(
            button.x, button.y, radius,
            if (faint) faintBorderPaint else borderPaint
        )

        textPaint.color = if (pressed) Color.rgb(60, 40, 0) else Color.WHITE
        textPaint.alpha = if (faint) 110 else 255
        textPaint.textSize = if (button.label.length > 2) {
            button.radius * 0.48f
        } else {
            button.radius * 0.9f
        }

        val metrics = textPaint.fontMetrics
        val baseline = button.y - (metrics.ascent + metrics.descent) / 2f

        canvas.drawText(button.label, button.x, baseline, textPaint)
    }

    private fun buttonAt(x: Float, y: Float, l: Layout): Int {
        var held = 0
        for (button in l.buttons) {
            val dx = x - button.x
            val dy = y - button.y

            if (dx * dx + dy * dy <= button.radius * button.radius * 1.7f) {
                held = held or button.mask
            }
        }
        if (held != 0) return held

        val dx = x - l.dpadX
        val dy = y - l.dpadY
        val radius = l.dpadRadius * 1.25f

        if (dx * dx + dy * dy <= radius * radius) {
            var result = 0
            val deadzone = l.dpadRadius * 0.22f

            if (dx < -deadzone) result = result or LEFT
            if (dx > deadzone) result = result or RIGHT
            if (dy < -deadzone) result = result or UP
            if (dy > deadzone) result = result or DOWN

            return result
        }

        return 0
    }

    private fun updatePointer(id: Int, x: Float, y: Float) {
        val l = layout() ?: return
        pointers[id] = buttonAt(x, y, l)
    }

    private fun publishInput() {
        val mask = pointers.values.fold(0) { total, value ->
            total or value
        }
        if (mask != activeMask) {
            activeMask = mask
            invalidate()
        }
        onInputChanged(mask)
    }

    override fun onTouchEvent(event: MotionEvent): Boolean {
        when (event.actionMasked) {
            MotionEvent.ACTION_DOWN,
            MotionEvent.ACTION_POINTER_DOWN -> {
                val i = event.actionIndex
                val id = event.getPointerId(i)
                val x = event.getX(i)
                val y = event.getY(i)

                updatePointer(id, x, y)

                publishInput()
            }

            MotionEvent.ACTION_MOVE -> {
                for (i in 0 until event.pointerCount) {
                    updatePointer(event.getPointerId(i), event.getX(i), event.getY(i))
                }
                publishInput()
            }

            MotionEvent.ACTION_POINTER_UP -> {
                val i = event.actionIndex
                val id = event.getPointerId(i)

                pointers.remove(id)

                for (j in 0 until event.pointerCount) {
                    if (j == i) continue

                    updatePointer(event.getPointerId(j), event.getX(j), event.getY(j))
                }

                publishInput()
            }

            MotionEvent.ACTION_UP,
            MotionEvent.ACTION_CANCEL -> {
                pointers.clear()
                publishInput()
            }
        }

        return true
    }
}
