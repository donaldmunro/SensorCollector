#include <stdio.h>

#include <cstddef>
#include <string>
#include <iostream>
#include <sstream>
#include <algorithm>
#include <limits>
#include <chrono>
#include <memory>
#include <atomic>
#include <mutex>
#include <thread>

#include <dlfcn.h>
#include <jni.h>
#include <android/sensor.h>

#include "native-lib.h"
#define ANDROID_LOG_TAG "SensorCollector"
#include "android-logging.h"
#include "latch.hpp"


std::string packageName("");
jobject activity = nullptr;
jmethodID callbackId = nullptr;
JavaVM* javaVM = nullptr;

std::atomic<bool> stop(false);
std::mutex stats_mutex;
std::atomic<int> threadcount(0);
std::shared_ptr<stdex::latch> latchptr;

void sensor_thread(int sensorId, int rate, long ms, std::string &&pathname, std::string &&statspath,
                   jint format, jboolean isTs)
//-----------------------------------------------------------------------------------------
{
   threadcount++;
   std::stringstream message;
   bool status = true;
   JavaVMAttachArgs javaVMAttachArgs;
   javaVMAttachArgs.version = JNI_VERSION_1_6;
   javaVMAttachArgs.name = "native-lib";
   javaVMAttachArgs.group = NULL;
   JNIEnv *env;
   if (javaVM->AttachCurrentThreadAsDaemon(&env, &javaVMAttachArgs) != JNI_OK)
   {
      env = nullptr;
      ALOGE("native_lib::sensor_thread: Error attaching thread to JVM");
   }

   FILE *fd = fopen(pathname.c_str(), "we");
   if (fd == nullptr)
   {
      message << "native-lib::SensorThread: Error opening file " << pathname;
      status = false;
      error(env, message.str().c_str(), "sensor_thread");
      if (env != nullptr) javaVM->DetachCurrentThread();
      latchptr->count_down_and_wait();
      threadcount--;
      return;
   }
   setvbuf(fd, nullptr, _IOFBF, 65535);
   std::string sensor_name = get_sensor_name(sensorId);
   ALooper *looper = ALooper_prepare(ALOOPER_PREPARE_ALLOW_NON_CALLBACKS);
   if (looper == nullptr)
   {
      message << "native-lib::SensorThread: Error getting looper";
      error(env, message.str().c_str(), "sensor_thread");
      if (env != nullptr) javaVM->DetachCurrentThread();
      fclose(fd);
      latchptr->count_down_and_wait();
      threadcount--;
      return;
   }
   ASensorManager *sensor_manager = getSensorManager(packageName.c_str());
   if (sensor_manager == nullptr)
   {
      message << "native-lib::SensorThread: Error getting SensorManager ";
      fclose(fd);
      error(env, message.str().c_str(), "sensor_thread");
      if (env != nullptr) javaVM->DetachCurrentThread();
      latchptr->count_down_and_wait();
      threadcount--;
      return;
   }
   ASensorEventQueue *queue = ASensorManager_createEventQueue(sensor_manager, looper, LOOPER_ID_USER, nullptr, nullptr);
   if (queue == nullptr)
   {
      message << "native-lib::SensorThread: Error obtaining " << sensor_name << " events queue";
      fclose(fd);
      error(env, message.str().c_str(), "sensor_thread");
      if (env != nullptr) javaVM->DetachCurrentThread();
      latchptr->count_down_and_wait();
      threadcount--;
      return;
   }
   const ASensor *sensor = ASensorManager_getDefaultSensor(sensor_manager, sensorId /*ASENSOR_TYPE_GRAVITY*/);
   if (sensor == nullptr)
   {
      message << "native-lib::SensorThread: Error obtaining " << sensor_name << " sensor.";
      ASensorManager_destroyEventQueue(sensor_manager, queue);
      fclose(fd);
      error(env, message.str().c_str(), "sensor_thread");
      if (env != nullptr) javaVM->DetachCurrentThread();
      latchptr->count_down_and_wait();
      threadcount--;
      return;
   }
   int bestrate = ASensor_getMinDelay(sensor);
   if (bestrate <= 0)
   {
      message << "native-lib::SensorThread: Error obtaining best sampling rate for " << sensor_name << " sensor.";
      ASensorManager_destroyEventQueue(sensor_manager, queue);
      fclose(fd);
      error(env, message.str().c_str(), "sensor_thread");
      if (env != nullptr) javaVM->DetachCurrentThread();
      latchptr->count_down_and_wait();
      threadcount--;
      return;
   }
   if ((rate <= 0) || (rate > bestrate))
      rate = bestrate;

   int sensor_status = ASensorEventQueue_enableSensor(queue, sensor);
   if (sensor_status < 0)
   {
      message << "native-lib::SensorThread: Error enabling " << sensor_name << " sensor";
      status = false;
      ASensorManager_destroyEventQueue(sensor_manager, queue);
      fclose(fd);
      error(env, message.str().c_str(), "sensor_thread");
      if (env != nullptr) javaVM->DetachCurrentThread();
      latchptr->count_down_and_wait();
      threadcount--;
      return;
   }
   sensor_status = ASensorEventQueue_setEventRate(queue, sensor, rate);
   if (sensor_status < 0)
   {
      message << "native-lib: Error setting rate to " << rate << " for " << sensor_name << " sensor";
      ASensorEventQueue_disableSensor(queue, sensor);
      ASensorManager_destroyEventQueue(sensor_manager, queue);
      status = false;
      fclose(fd);
      error(env, message.str().c_str(), "sensor_thread");
      if (env != nullptr) javaVM->DetachCurrentThread();
      latchptr->count_down_and_wait();
      threadcount--;
      return;
   }
   ASensorEvent event;
   int fields = 3;
   switch (sensorId)
   {
      case ASENSOR_TYPE_GRAVITY:
      case ASENSOR_TYPE_LINEAR_ACCELERATION:
      case ASENSOR_TYPE_ACCELEROMETER:
      case ASENSOR_TYPE_GYROSCOPE:
      case ASENSOR_TYPE_MAGNETIC_FIELD:
         fields = 3;
         break;
      case ASENSOR_TYPE_ACCELEROMETER_UNCALIBRATED:
      case ASENSOR_TYPE_GYROSCOPE_UNCALIBRATED:
      case ASENSOR_TYPE_MAGNETIC_FIELD_UNCALIBRATED:
         fields = 6;
         break;
   }

   std::unique_ptr<Formatter> formatter(make_formatter(format, (isTs == JNI_TRUE)));
   auto then = std::chrono::high_resolution_clock::now();
   long elapsed = 0, n = 0;
   long fts = -1, lts = -1;
   long long tsavg = 0;
   latchptr->count_down_and_wait();
   do
   {
      ALooper_pollAll(50, nullptr, nullptr, nullptr);
      if (ASensorEventQueue_hasEvents(queue) > 0)
      {
         while (ASensorEventQueue_getEvents(queue, &event, 1) > 0)
         {
            long ts = event.timestamp;
            if (fts < 0)
            {
              fts = ts;
              ts = 0;
            }
            else
            {
               ts -= fts;
               ts /= 1000000; //ns -> ms
            }
            fprintf(fd, "%s\n", formatter->format(ts, fields, event.data).c_str());
            if (lts > 0)
               tsavg += (ts - lts);
            lts = ts;
            n++;
         }
      }
      auto now = std::chrono::high_resolution_clock::now();
      elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - then).count();
   } while ((elapsed < ms) && (!stop.load()));
   fclose(fd);
   ASensorEventQueue_disableSensor(queue, sensor);
   ASensorManager_destroyEventQueue(sensor_manager, queue);
   status = true;

   {
      std::lock_guard<std::mutex> lock(stats_mutex);
      fd = fopen(statspath.c_str(), "ae");
      if (fd != nullptr)
      {
         fprintf(fd, "%s: %ld samples @ Average Sample Rate (microseconds) = %lld\n", sensor_name.c_str(), n,
                 tsavg / n);
         fclose(fd);
      }
   }
   if (env != nullptr)
   {
      std::string name = get_sensor_name(sensorId);
      message << "Sensor " << name << " complete. " << n << " samples written to " << pathname;
      jstring jstr = env->NewStringUTF(message.str().c_str());
      env->CallVoidMethod(activity, callbackId, sensorId, jstr);
      env->DeleteLocalRef(jstr);
//      javaVM->DetachCurrentThread();
   }
   threadcount--;
}

extern "C"
{
   JNIEXPORT jboolean JNICALL Java_sensor_collector_MainActivity_00024Companion_initCollector
      (JNIEnv *env, jobject obj, jobject instance, jstring package, jstring callback)
   //------------------------------------------------------------------------------------------------------------------
   {
      if (env->GetJavaVM(&javaVM) != JNI_OK)
      {
         ALOGE("native-lib::initCollector: Error obtaining Java VM");
         return JNI_FALSE;
      }
      const char *psz = env->GetStringUTFChars(package, 0);
      ASensorManager *sensor_manager = getSensorManager(psz);
      if (sensor_manager == nullptr)
      {
         ALOGE("native-lib::initCollector: Error obtaining sensor manager using package");
         env->ReleaseStringUTFChars(package, psz);
         return JNI_FALSE;
      }

      packageName = psz;
      env->ReleaseStringUTFChars(package, psz);

      activity = env->NewGlobalRef(instance);
      if (activity == nullptr)
      {
         ALOGE("native-lib::initCollector: Could not obtain activity reference");
         return JNI_FALSE;
      }
      jclass cls = env->GetObjectClass(activity);
      if (cls == nullptr)
      {
         ALOGE("native-lib::initCollector: Could not obtain activity class");
         return JNI_FALSE;
      }

      psz = env->GetStringUTFChars(callback, 0);
      callbackId = env->GetMethodID(cls, psz, "(ILjava/lang/String;)V");
      if (callbackId == nullptr)
      {
         ALOGE("native-lib::initCollector: Error finding method named %s", psz);
         env->ReleaseStringUTFChars(callback, psz);
         return JNI_FALSE;
      }
      env->ReleaseStringUTFChars(callback, psz);

      jstring jstr = env->NewStringUTF("JNI Collector initialized.");
      env->CallVoidMethod(activity, callbackId, -1, jstr);
      env->DeleteLocalRef(jstr);

      stop.store(false);
      return JNI_TRUE;
   }


   JNIEXPORT void JNICALL Java_sensor_collector_MainActivity_00024Companion_stopCollecting
      (JNIEnv *, jobject)
   //------------------------------------------------------------------------------------
   {
      stop.store(true);
      if (threadcount.load() > 0)
      {
         stop.store(true);
         std::this_thread::sleep_for(std::chrono::milliseconds(500));
      }
      if (threadcount.load() == 0)
         stop.store(false);
   }

   JNIEXPORT void JNICALL Java_sensor_collector_MainActivity_00024Companion_closeCollector
      (JNIEnv* env, jobject)
   //------------------------------------------------------------------------------------
   {
      if (activity != nullptr)
         env->DeleteGlobalRef(activity);
      activity = nullptr;
      packageName = "";
   }

   JNIEXPORT jint JNICALL Java_sensor_collector_MainActivity_00024Companion_bestRate
         (JNIEnv* env, jobject, jint sensorId)
   //--------------------------------------------------------------------------------------
   {
      ASensorManager *sensor_manager = getSensorManager(packageName.c_str());
      if (sensor_manager == nullptr)
      {
         error(env, "Error obtaining sensor manager", "bestRate");
         return -1;
      }
      const ASensor* sensor = ASensorManager_getDefaultSensor(sensor_manager, sensorId /*ASENSOR_TYPE_GRAVITY*/);
      if (sensor != nullptr)
         return ASensor_getMinDelay(sensor);
      else
      {
         error(env, "Error obtaining sensor", "bestRate");
         return -1;
      }
   }


   //fun collect(sensors: IntArray, basePath: String, seconds: Double, rate: Int =0, format: Int =0,
//                   is_include_timestamp: Boolean =false): Boolean
   JNIEXPORT jboolean JNICALL Java_sensor_collector_MainActivity_00024Companion_collect
      (JNIEnv *env, jobject obj, jintArray sensorIds, jstring basePath, jdouble seconds, jint rate,
            jint format, jboolean isTs)
   //----------------------------------------------------------------------------------------------
   {
      if (threadcount.load() > 0)
      {
         stop.store(true);
         std::this_thread::yield(); //sleep_for(std::chrono::milliseconds(20));
      }
      const char* psz_filepath = env->GetStringUTFChars(basePath, 0);
      std::string file_path(psz_filepath), extension;
      env->ReleaseStringUTFChars(basePath, psz_filepath);
      auto extpos = file_path.find_last_of('.');
      if (extpos != std::string::npos)
      {
         extension = file_path.substr(extpos);
         file_path = file_path.substr(0, extpos);
      }
      else
         extension = "";
      long ms = (seconds > 0) ? static_cast<long>(seconds*1000) : std::numeric_limits<long>::max();
      jsize no_sensors = env->GetArrayLength(sensorIds);
      jint *sensors = env->GetIntArrayElements(sensorIds, nullptr);
      if (threadcount.load() > 0)
      {
         stop.store(true);
         std::this_thread::sleep_for(std::chrono::milliseconds(300));
      }
      latchptr.reset(new stdex::latch(static_cast<size_t>(no_sensors)));
      stop.store(false);
      for (int i=0; i<no_sensors; i++)
      {
         int  sensor = sensors[i];
         std::string output_pathname =file_path, stats_pathname =file_path, sensor_name = get_sensor_name(sensor);
         std::replace( sensor_name.begin(), sensor_name.end(), ' ', '_');
         output_pathname.append("-").append(sensor_name).append(extension);
         stats_pathname.append("-stats").append(extension);
//         const char *pstr = strdup(output_pathname.c_str());
         //strdup(stats_pathname.c_str())
         std::thread t(&sensor_thread, sensor, rate, ms, std::move(output_pathname), std::move(stats_pathname), format, isTs);
         t.detach();
      }
      env->ReleaseIntArrayElements(sensorIds, sensors, 0);
      return JNI_TRUE;
   }
}

ASensorManager* getSensorManager(const char* packageName)
//------------------------------------------------------
{
   typedef ASensorManager *(*PF_GETINSTANCEFORPACKAGE)(const char *name);
   void *androidHandle = dlopen("libandroid.so", RTLD_NOW);
   PF_GETINSTANCEFORPACKAGE getInstanceForPackageFunc = (PF_GETINSTANCEFORPACKAGE) dlsym(androidHandle,
                                                                                         "ASensorManager_getInstanceForPackage");
   if (getInstanceForPackageFunc)
      return getInstanceForPackageFunc(packageName);

   typedef ASensorManager *(*PF_GETINSTANCE)();
   PF_GETINSTANCE getInstanceFunc = (PF_GETINSTANCE) dlsym(androidHandle, "ASensorManager_getInstance");
   // by all means at this point, ASensorManager_getInstance should be available
   if (getInstanceFunc == nullptr) return nullptr;
   return getInstanceFunc();
}

std::string get_sensor_name(int sensorID)
//----------------------------------------
{
   switch (sensorID)
   {
      case ASENSOR_TYPE_GRAVITY:                      return "Gravity";
      case ASENSOR_TYPE_LINEAR_ACCELERATION:          return "Linear Accelerometer";
      case ASENSOR_TYPE_ACCELEROMETER:                return "Accelerometer";
      case ASENSOR_TYPE_GYROSCOPE:                    return "Gyroscope";
      case ASENSOR_TYPE_MAGNETIC_FIELD:               return "Magnetic";
      case ASENSOR_TYPE_ACCELEROMETER_UNCALIBRATED:   return "Raw Accelerometer";
      case ASENSOR_TYPE_GYROSCOPE_UNCALIBRATED:       return "Raw Gyroscope";
      case ASENSOR_TYPE_MAGNETIC_FIELD_UNCALIBRATED:  return "Raw Magnetic";
   }
   return "Unknown";
}

void error(JNIEnv *env, const char* message, const char* origin)
//--------------------------------------------------------------
{
   if ( (activity != nullptr) && (callbackId != nullptr) && (env != nullptr) )
   {
      jstring jstr;
      jstr = env->NewStringUTF(message);
      env->CallVoidMethod(activity, callbackId, -1, jstr);
      env->DeleteLocalRef(jstr);
   }
   else
      ALOGE("native-lib::%s: %s", origin, message);
}
