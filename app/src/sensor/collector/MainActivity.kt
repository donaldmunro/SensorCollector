package sensor.collector

import android.Manifest
import android.app.Activity
import android.app.AlertDialog
import android.content.Context
import android.content.pm.PackageManager
import android.hardware.Sensor
import android.os.Build
import android.os.Bundle
import android.os.Environment
import android.view.View
import android.widget.ArrayAdapter
import android.widget.CheckBox
import android.widget.Toast
import kotlinx.android.synthetic.main.activity_main.*
import java.io.File
import java.util.concurrent.atomic.AtomicInteger

class MainActivity : Activity()
{
   private lateinit var directory: File
   private lateinit var sensorCheckboxes: MutableList<Pair<CheckBox, Int>>
   private var runningSensorCount: AtomicInteger = AtomicInteger(0)
   private var isStoppingCollection: Boolean = false

   override fun onCreate(savedInstanceState: Bundle?)
   //------------------------------------------------
   {
      super.onCreate(savedInstanceState)
      setContentView(R.layout.activity_main)
      checkPermissions()
      var packageName = MainActivity::class.java.name  //.javaClass.name
      packageName = packageName.substring(0, packageName.lastIndexOf("."))
      if (! initCollector(this, packageName, "callback"))
         message("Initialization of native library failed", true)
      ArrayAdapter.createFromResource(this, R.array.format_array, android.R.layout.simple_spinner_item).
            also { adapter -> adapter.setDropDownViewResource(android.R.layout.simple_spinner_dropdown_item); spinFormat.adapter = adapter }
      sensorCheckboxes = listOf(Pair(checkboxGravity, Sensor.TYPE_GRAVITY),
                                Pair(checkboxAccel, Sensor.TYPE_ACCELEROMETER),
                                Pair(checkboxLinearAccel, Sensor.TYPE_LINEAR_ACCELERATION),
                                Pair(checkboxGyro, Sensor.TYPE_GYROSCOPE),
                                Pair(checkboxRawGyro, Sensor.TYPE_GYROSCOPE_UNCALIBRATED),
                                Pair(checkboxMagnetic, Sensor.TYPE_MAGNETIC_FIELD),
                                Pair(checkboxRawMagnetic, Sensor.TYPE_MAGNETIC_FIELD_UNCALIBRATED)).toMutableList()
      if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O)
         sensorCheckboxes.add(sensorCheckboxes.size, Pair(checkboxRawAccel, Sensor.TYPE_ACCELEROMETER_UNCALIBRATED))
      else
         checkboxRawAccel.visibility  = View.GONE
      textMessages.keyListener = null
      var rate = bestRate(Sensor.TYPE_GRAVITY)
      if (rate < 0)
         rate = 4000
      editRate?.setText("$rate")
      buttonStart.setOnClickListener { onNDKStartClicked() }
   }

   override fun onDestroy()
   //----------------------
   {
      super.onDestroy()
      stopCollecting()
      Thread.sleep(50)
      closeCollector()
   }

   private fun getSensors(): Pair<IntArray, Array<String>>
   //------------------------------------------------
   {
      val sensorIds = mutableListOf<Int>()
      val sensorNames = mutableListOf<String>()
      for (pp in sensorCheckboxes)
      {
         val checkbox = pp.first
         if (checkbox.isChecked)
         {
            sensorIds.add(pp.second)
            sensorNames.add(checkbox.text.toString())
         }
      }
      return Pair<IntArray, Array<String>>(sensorIds.toIntArray(), sensorNames.toTypedArray())
   }

   private fun getFormat(): Int
   //--------------------------
   {
      val i = spinFormat.selectedItemId.toInt()
      if (i < 0) return -1
      val format = spinFormat.adapter.getItem(i)
      return when (format)
      {
         getString(R.string.format_matlab) -> 0
         getString(R.string.format_python) -> 1
         else -> -1
      }
   }

   public fun callback(sensorId: Int, message: String?)
   //-------------------------------------------------
   {
      if ( (message != null) && (! message.trim().isEmpty()) )
         runOnUiThread { val s = textMessages.text.toString() + "\n" + message; textMessages.setText(s); }
      if (sensorId > 0)
      {
         if (runningSensorCount.decrementAndGet() == 0)
         {
            isStoppingCollection = false
            runOnUiThread { buttonStart.text = getString(R.string.start_ndk) }
         }
      }
   }

   private fun onNDKStartClicked()
   //--------------------------
   {
      if ( (isStoppingCollection) || (buttonStart.text.toString() == getString(R.string.stop)) )
      {
         if (! isStoppingCollection)
         {
            stopCollecting();
            isStoppingCollection = true;
         }
         return;
      }
      val time:Double? = editTime.text.toString().trim().toDoubleOrNull()
      if (time == null)
      {
         message("Invalid time")
         return
      }
      var name = editFilename.text.toString().trim()
      if (name.isEmpty())
      {
         message("Specify filename in Document directory")
         return
      }
      var f = File(name)
      name = f.name
      f = File(directory, name)
      if (f.exists())
         f.delete()

      var s = editRate.text.toString().trim()
      var rate = try { s.toInt() } catch (e: Exception) { -1 }
      val bestrate = bestRate(Sensor.TYPE_GRAVITY)
      if (rate < 0)
         rate = bestrate
      val (sensorIds, _) = getSensors()
      val format = getFormat()
      if (format < 0)
      {
         message("Invalid format")
         return
      }
//      val callback: Method? = MainActivity::class.java.methods.find { m -> m.name == "callback" }
      runningSensorCount.set(sensorIds.size)
      when (sensorIds.size)
      {
         0 -> { message("ERROR: No sensors selected"); return; }
         else ->
            {
               textMessages.setText("")
               if (collect(sensorIds, f.absolutePath, time, rate, format, checkboxTimestamp.isChecked))
                  buttonStart.text = getString(R.string.stop)
            }
      }
   }

    private fun checkPermissions()
   //----------------------------
   {
      var permission = checkSelfPermission(Manifest.permission.WRITE_EXTERNAL_STORAGE)
      if (permission == PackageManager.PERMISSION_GRANTED)
      {
         directory = Environment.getExternalStoragePublicDirectory(Environment.DIRECTORY_DOCUMENTS)
         directory.mkdirs()
      }
      else
         requestPermissions(arrayOf(Manifest.permission.WRITE_EXTERNAL_STORAGE), 1)
   }

   override fun onRequestPermissionsResult(requestCode: Int, permissions: Array<String>, grantResults: IntArray)
   //-----------------------------------------------------------------------------------------
   {
      when (requestCode)
      {
         1 ->
         {
            if ( (! grantResults.isEmpty()) && (grantResults[0] != PackageManager.PERMISSION_GRANTED) )
               message("Cannot record without file write permissions", true)
            else
            {
               directory = Environment.getExternalStoragePublicDirectory(Environment.DIRECTORY_DOCUMENTS)
               directory.mkdirs()
            }
         }
      }
   }

   private fun message(message: String, isExit: Boolean =false)
   //------------------------------
   {
      val alert = AlertDialog.Builder(this)
      alert.setTitle("Exit")
      alert.setMessage(message)
      alert.setPositiveButton("Ok") { _, _ -> if (isExit) this@MainActivity.finish() }
      alert.show()
   }

   private fun Context.toast(message: CharSequence) = Toast.makeText(this, message, Toast.LENGTH_LONG).show()

   companion object
   {
      init
      {
         System.loadLibrary("native-lib")
      }

      external fun initCollector(instance: Activity, packageName: String, callback: String): Boolean

      external fun bestRate(sensor: Int) : Int


      external fun collect(sensors: IntArray, basePath: String, seconds: Double, rate: Int =0, format: Int =0,
                           is_include_timestamp: Boolean =false): Boolean

      external fun stopCollecting()

      external fun closeCollector()
   }

}
