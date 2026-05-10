package com.guitarfx.ui.effects

import android.graphics.Color
import android.os.Bundle
import android.os.Handler
import android.os.Looper
import android.view.LayoutInflater
import android.view.View
import android.view.ViewGroup
import android.widget.*
import androidx.fragment.app.Fragment
import com.guitarfx.R
import com.guitarfx.audio.AudioBridge

class EffectsFragment : Fragment() {

    private val handler = Handler(Looper.getMainLooper())
    private var monitorRunnable: Runnable? = null

    // Views
    private var btnStartStop: Button? = null
    private var tvStatus: TextView? = null
    private var vuIn: ProgressBar? = null
    private var vuOut: ProgressBar? = null

    // Preset buttons
    private var presetGroup: RadioGroup? = null

    // Effect cards
    private var cardDist: View? = null
    private var cardEQ: View? = null
    private var cardDelay: View? = null
    private var cardReverb: View? = null
    private var cardChorus: View? = null
    private var cardShimmer: View? = null

    override fun onCreateView(inflater: LayoutInflater, container: ViewGroup?, savedInstanceState: Bundle?): View {
        return inflater.inflate(R.layout.fragment_effects, container, false)
    }

    override fun onViewCreated(view: View, savedInstanceState: Bundle?) {
        super.onViewCreated(view, savedInstanceState)

        btnStartStop = view.findViewById(R.id.btn_start_stop)
        tvStatus = view.findViewById(R.id.tv_status)
        vuIn = view.findViewById(R.id.vu_in)
        vuOut = view.findViewById(R.id.vu_out)
        presetGroup = view.findViewById(R.id.preset_group)

        setupPresets(view)
        setupCards(view)
        setupControls(view)

        btnStartStop?.setOnClickListener {
            if (AudioBridge.nativeIsRunning()) {
                AudioBridge.nativeStop()
                btnStartStop?.text = "START"
                btnStartStop?.setBackgroundColor(Color.parseColor("#4CAF50"))
                stopMonitor()
            } else {
                val ok = AudioBridge.nativeStart(2) // 2 = speaker
                if (ok) {
                    btnStartStop?.text = "STOP"
                    btnStartStop?.setBackgroundColor(Color.parseColor("#F44336"))
                    startMonitor()
                } else {
                    Toast.makeText(context, "Failed to start audio engine", Toast.LENGTH_SHORT).show()
                }
            }
        }
    }

    private fun setupPresets(view: View) {
        val presets = listOf(
            R.id.preset_clean to AudioBridge.PRESET_CLEAN,
            R.id.preset_overdrive to AudioBridge.PRESET_OVERDRIVE,
            R.id.preset_distortion to AudioBridge.PRESET_DISTORTION,
            R.id.preset_fuzz to AudioBridge.PRESET_FUZZ,
            R.id.preset_postrock to AudioBridge.PRESET_POST_ROCK,
            R.id.preset_wall to AudioBridge.PRESET_WALL_OF_SOUND
        )

        for ((buttonId, presetId) in presets) {
            view.findViewById<RadioButton>(buttonId)?.setOnClickListener {
                AudioBridge.nativeLoadPreset(presetId)
                updateCardStates()
            }
        }

        // Default: distortion
        view.findViewById<RadioButton>(R.id.preset_distortion)?.isChecked = true
    }

    private fun setupCards(view: View) {
        cardDist = view.findViewById(R.id.card_distortion)
        cardEQ = view.findViewById(R.id.card_eq)
        cardDelay = view.findViewById(R.id.card_delay)
        cardReverb = view.findViewById(R.id.card_reverb)
        cardChorus = view.findViewById(R.id.card_chorus)
        cardShimmer = view.findViewById(R.id.card_shimmer)

        // Toggle switches for each effect
        setupToggle(view, R.id.switch_dist, AudioBridge.FX_DIST)
        setupToggle(view, R.id.switch_eq, AudioBridge.FX_EQ)
        setupToggle(view, R.id.switch_delay, AudioBridge.FX_DELAY)
        setupToggle(view, R.id.switch_reverb, AudioBridge.FX_REVERB)
        setupToggle(view, R.id.switch_chorus, AudioBridge.FX_CHORUS)
        setupToggle(view, R.id.switch_shimmer, AudioBridge.FX_SHIMMER)
    }

    private fun setupToggle(view: View, switchId: Int, fxId: Int) {
        val sw = view.findViewById<Switch>(switchId)
        sw?.setOnCheckedChangeListener { _, isChecked ->
            AudioBridge.nativeSetEffectEnabled(fxId, isChecked)
        }
    }

    private fun setupControls(view: View) {
        // Gain slider
        val seekGain = view.findViewById<SeekBar>(R.id.seek_gain)
        seekGain?.setOnSeekBarChangeListener(object : SeekBar.OnSeekBarChangeListener {
            override fun onProgressChanged(seekBar: SeekBar?, progress: Int, fromUser: Boolean) {
                if (fromUser) {
                    val gain = progress / 10.0f // 0.0 - 10.0
                    AudioBridge.nativeSetGain(gain)
                }
            }
            override fun onStartTrackingTouch(seekBar: SeekBar?) {}
            override fun onStopTrackingTouch(seekBar: SeekBar?) {}
        })
        seekGain?.progress = 50 // default gain 5.0

        // Volume slider
        val seekVol = view.findViewById<SeekBar>(R.id.seek_volume)
        seekVol?.setOnSeekBarChangeListener(object : SeekBar.OnSeekBarChangeListener {
            override fun onProgressChanged(seekBar: SeekBar?, progress: Int, fromUser: Boolean) {
                if (fromUser) {
                    AudioBridge.nativeSetVolume(progress / 100.0f)
                }
            }
            override fun onStartTrackingTouch(seekBar: SeekBar?) {}
            override fun onStopTrackingTouch(seekBar: SeekBar?) {}
        })
        seekVol?.progress = 100

        // Drive slider
        val seekDrive = view.findViewById<SeekBar>(R.id.seek_drive)
        seekDrive?.setOnSeekBarChangeListener(object : SeekBar.OnSeekBarChangeListener {
            override fun onProgressChanged(seekBar: SeekBar?, progress: Int, fromUser: Boolean) {
                if (fromUser) {
                    val drive = progress / 5.0f // 0.0 - 20.0
                    AudioBridge.nativeSetDistortion(2, drive, 0.5f)
                }
            }
            override fun onStartTrackingTouch(seekBar: SeekBar?) {}
            override fun onStopTrackingTouch(seekBar: SeekBar?) {}
        })
        seekDrive?.progress = 40 // default drive 8.0

        // Delay time
        val seekDelayTime = view.findViewById<SeekBar>(R.id.seek_delay_time)
        seekDelayTime?.setOnSeekBarChangeListener(object : SeekBar.OnSeekBarChangeListener {
            override fun onProgressChanged(seekBar: SeekBar?, progress: Int, fromUser: Boolean) {
                if (fromUser) {
                    val timeMs = progress * 10.0f // 0 - 1000ms
                    AudioBridge.nativeSetDelay(timeMs, 0.4f, 0.3f)
                }
            }
            override fun onStartTrackingTouch(seekBar: SeekBar?) {}
            override fun onStopTrackingTouch(seekBar: SeekBar?) {}
        })

        // Reverb mix
        val seekReverbMix = view.findViewById<SeekBar>(R.id.seek_reverb_mix)
        seekReverbMix?.setOnSeekBarChangeListener(object : SeekBar.OnSeekBarChangeListener {
            override fun onProgressChanged(seekBar: SeekBar?, progress: Int, fromUser: Boolean) {
                if (fromUser) {
                    val mix = progress / 100.0f
                    AudioBridge.nativeSetReverb(0.5f, 0.3f, mix)
                }
            }
            override fun onStartTrackingTouch(seekBar: SeekBar?) {}
            override fun onStopTrackingTouch(seekBar: SeekBar?) {}
        })

        // Bypass button
        val btnBypass = view.findViewById<Button>(R.id.btn_bypass)
        btnBypass?.setOnClickListener {
            val tag = it.tag as? Boolean ?: false
            val newState = !tag
            it.tag = newState
            AudioBridge.nativeSetBypass(newState)
            btnBypass.text = if (newState) "BYPASS ON" else "BYPASS"
            btnBypass.setBackgroundColor(
                if (newState) Color.parseColor("#FF9800") else Color.parseColor("#607D8B")
            )
        }
    }

    private fun updateCardStates() {
        // After preset load, update switch states to match
        // This is simplified - in production you'd query actual state from native
    }

    private fun startMonitor() {
        monitorRunnable = object : Runnable {
            override fun run() {
                if (!AudioBridge.nativeIsRunning()) return
                val peakIn = AudioBridge.nativeGetPeakIn()
                val peakOut = AudioBridge.nativeGetPeakOut()
                val xruns = AudioBridge.nativeGetXruns()

                vuIn?.progress = (peakIn * 100).toInt().coerceIn(0, 100)
                vuOut?.progress = (peakOut * 100).toInt().coerceIn(0, 100)
                tvStatus?.text = "Xruns: $xruns"

                handler.postDelayed(this, 200)
            }
        }
        handler.post(monitorRunnable!!)
    }

    private fun stopMonitor() {
        monitorRunnable?.let { handler.removeCallbacks(it) }
        monitorRunnable = null
    }

    override fun onDestroyView() {
        super.onDestroyView()
        stopMonitor()
    }
}
