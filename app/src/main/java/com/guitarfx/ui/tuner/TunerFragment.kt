package com.guitarfx.ui.tuner

import android.graphics.Canvas
import android.graphics.Color
import android.graphics.Paint
import android.os.Handler
import android.os.Looper
import android.view.LayoutInflater
import android.view.View
import android.view.ViewGroup
import android.widget.FrameLayout
import android.widget.TextView
import androidx.fragment.app.Fragment
import com.guitarfx.audio.AudioBridge

class TunerFragment : Fragment() {

    private val handler = Handler(Looper.getMainLooper())
    private var tunerRunnable: Runnable? = null
    private var tunerView: TunerView? = null
    private var tvFreq: TextView? = null
    private var tvNote: TextView? = null
    private var tvCents: TextView? = null

    private val noteNames = arrayOf("C","C#","D","D#","E","F","F#","G","G#","A","A#","B")

    override fun onCreateView(inflater: LayoutInflater, container: ViewGroup?, savedInstanceState: android.os.Bundle?): View {
        val layout = FrameLayout(requireContext())

        tvNote = TextView(context).apply {
            text = "--"
            textSize = 72f
            setTextColor(Color.WHITE)
            textAlignment = View.TEXT_ALIGNMENT_CENTER
        }

        tvFreq = TextView(context).apply {
            text = "0.0 Hz"
            textSize = 24f
            setTextColor(Color.GRAY)
            textAlignment = View.TEXT_ALIGNMENT_CENTER
            y = 300f
        }

        tvCents = TextView(context).apply {
            text = "+0 cents"
            textSize = 20f
            setTextColor(Color.GRAY)
            textAlignment = View.TEXT_ALIGNMENT_CENTER
            y = 350f
        }

        tunerView = TunerView(requireContext())

        layout.addView(tunerView)
        layout.addView(tvNote)
        layout.addView(tvFreq)
        layout.addView(tvCents)

        return layout
    }

    override fun onResume() {
        super.onResume()
        startTuner()
    }

    override fun onPause() {
        super.onPause()
        stopTuner()
    }

    private fun startTuner() {
        tunerRunnable = object : Runnable {
            override fun run() {
                if (!AudioBridge.nativeIsRunning()) {
                    handler.postDelayed(this, 100)
                    return
                }

                val freq = AudioBridge.nativeGetTunerFreq()
                val note = AudioBridge.nativeGetTunerNote()
                val cents = AudioBridge.nativeGetTunerCents()

                if (note >= 0 && note < 12) {
                    tvNote?.text = noteNames[note]
                    tvFreq?.text = String.format("%.1f Hz", freq)
                    tvCents?.text = String.format("%+.0f cents", cents)
                    tunerView?.setCents(cents)
                } else {
                    tvNote?.text = "--"
                    tvFreq?.text = "-- Hz"
                    tvCents?.text = ""
                    tunerView?.setCents(0f)
                }

                handler.postDelayed(this, 100)
            }
        }
        handler.post(tunerRunnable!!)
    }

    private fun stopTuner() {
        tunerRunnable?.let { handler.removeCallbacks(it) }
        tunerRunnable = null
    }

    // Custom view for tuner needle
    private class TunerView(context: android.content.Context) : View(context) {
        private val paint = Paint(Paint.ANTI_ALIAS_FLAG)
        private var cents: Float = 0f

        fun setCents(c: Float) {
            cents = c
            invalidate()
        }

        override fun onDraw(canvas: Canvas) {
            super.onDraw(canvas)
            val w = width.toFloat()
            val h = height.toFloat()
            val cx = w / 2
            val cy = h * 0.7f

            // Draw arc background
            paint.color = Color.parseColor("#333333")
            paint.style = Paint.Style.STROKE
            paint.strokeWidth = 4f
            val radius = w * 0.4f
            canvas.drawArc(cx - radius, cy - radius, cx + radius, cy + radius, 180f, 180f, false, paint)

            // Draw center mark (in tune)
            paint.color = Color.GREEN
            paint.strokeWidth = 3f
            canvas.drawLine(cx, cy - radius, cx, cy - radius + 20f, paint)

            // Draw needle
            val angle = (cents / 50f).coerceIn(-1f, 1f) * 90f // -90 to +90 degrees
            val needleAngle = Math.toRadians((180.0 - angle))
            val nx = cx + radius * 0.9f * Math.cos(needleAngle).toFloat()
            val ny = cy - radius * 0.9f * Math.sin(needleAngle).toFloat()

            paint.color = if (Math.abs(cents) < 5f) Color.GREEN else Color.YELLOW
            paint.strokeWidth = 3f
            canvas.drawLine(cx, cy, nx, ny, paint)
        }
    }
}
