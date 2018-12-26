#ifndef SENSOR_COLLECTOR_NATIVE_LIB_H
#define SENSOR_COLLECTOR_NATIVE_LIB_H

#include <string>
#include <android/sensor.h>

const int LOOPER_ID_USER = 3;

struct Formatter
{
   virtual std::string format(long timestamp, int no, float *values) =0;

   virtual ~Formatter() {}
};

struct MATLABFormatter : public Formatter
{
   MATLABFormatter(bool isTimestamp)
   {
      is_timestamp = isTimestamp;
   }

   std::string format(long timestamp, int no, float *values) override
   //-----------------------------------------------------------------
   {
      if (values == nullptr)
         return "";
      std::stringstream ss;
      if (is_timestamp) ss << timestamp << ",";
      ss << "[ ";
      for (int i=0; i<no; i++)
         ss << values[i] << ",";
      long pos = ss.tellp();
      ss.seekp (pos-1);
      ss << " ]";
      return ss.str();
   }

   bool is_timestamp;
};

struct CSVFormatter : public Formatter
{
    CSVFormatter(bool isTimestamp) : is_timestamp(isTimestamp) {}

    std::string format(long timestamp, int no, float *values) override
    //-----------------------------------------------------------------
    {
        if (values == nullptr)
            return "";
        std::stringstream ss;
        if (is_timestamp) ss << timestamp << ", ";
        for (int i=0; i<no; i++)
            ss << values[i] << ",";
        long pos = ss.tellp();
        ss.seekp (pos-1);
        ss << " ";
        return ss.str();
    }

    bool is_timestamp;
};

Formatter* make_formatter(int format, bool is_timestamped)
//-------------------------------------------------
{
   Formatter* formatter;
   switch (format)
   {
      case 1:  formatter = new CSVFormatter(is_timestamped); break;
      case 0:
      default: formatter = new MATLABFormatter(is_timestamped); break;
   }
   return formatter;
}

ASensorManager* getSensorManager(const char* packageName);
std::string get_sensor_name(int sensorID);

void error(JNIEnv *env, const char* message, const char* origin);

#endif
