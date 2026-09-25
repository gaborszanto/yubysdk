package com.example.yubydemo

import android.Manifest
import android.content.pm.PackageManager
import android.os.Bundle
import androidx.activity.result.contract.ActivityResultContracts
import androidx.appcompat.app.AppCompatActivity
import androidx.core.content.ContextCompat
import com.example.yubydemo.databinding.ActivityMainBinding
import java.io.File

class MainActivity : AppCompatActivity() {
    private lateinit var binding: ActivityMainBinding
    private lateinit var audioFile: File
    private var nativeStarted = false

    private val microphonePermission = registerForActivityResult(ActivityResultContracts.RequestPermission()) { granted ->
        if (!nativeStarted) startDemo(granted)
        else if (granted) {
            nativeSetInputAvailable(true)
            showMicrophoneState(nativeToggleMicrophone())
        }
    }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        binding = ActivityMainBinding.inflate(layoutInflater)
        setContentView(binding.root)
        audioFile = copyAssetToFiles("tropical-breeze.mp3")

        binding.playPauseButton.setOnClickListener {
            binding.playPauseButton.text = getString(if (nativeTogglePlayPause()) R.string.pause else R.string.play)
        }
        binding.reverbButton.setOnClickListener {
            binding.reverbButton.text = getString(if (nativeToggleReverb()) R.string.reverb_on else R.string.reverb_off)
        }
        binding.microphoneButton.setOnClickListener {
            if (hasMicrophonePermission()) showMicrophoneState(nativeToggleMicrophone())
            else microphonePermission.launch(Manifest.permission.RECORD_AUDIO)
        }

        if (hasMicrophonePermission()) startDemo(true)
        else microphonePermission.launch(Manifest.permission.RECORD_AUDIO)
    }

    override fun onDestroy() {
        if (nativeStarted) nativeStop()
        nativeStarted = false
        super.onDestroy()
    }

    private fun hasMicrophonePermission() = ContextCompat.checkSelfPermission(this, Manifest.permission.RECORD_AUDIO) == PackageManager.PERMISSION_GRANTED

    private fun startDemo(enableInput: Boolean) {
        nativeStart(audioFile.absolutePath, enableInput)
        nativeStarted = true
    }

    private fun showMicrophoneState(enabled: Boolean) {
        binding.microphoneButton.text = getString(if (enabled) R.string.mic_on else R.string.mic_off)
    }

    private fun copyAssetToFiles(name: String): File {
        val destination = File(filesDir, name)
        if (!destination.exists() || destination.length() == 0L) assets.open(name).use { input -> destination.outputStream().use { output -> input.copyTo(output) }}
        return destination
    }

    private external fun nativeStart(audioPath: String, enableInput: Boolean)
    private external fun nativeSetInputAvailable(available: Boolean)
    private external fun nativeTogglePlayPause(): Boolean
    private external fun nativeToggleReverb(): Boolean
    private external fun nativeToggleMicrophone(): Boolean
    private external fun nativeStop()

    companion object {
        init { System.loadLibrary("yubydemo") }
    }
}
