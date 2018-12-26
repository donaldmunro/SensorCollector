# SensorCollector
Simple application to collect readings from Android Sensors and
write them to a file for analysis or playback. Files are stored in the
(system) Documents directory (/sdcard/Documents on most systems.  The
sensor name is appended to the supplied name. If a recording time is
specified then the recording stops after the supplied number of
seconds. The sample rate defaults to the maximal supported one or
can be specified in microseconds. Selecting the "Include Timestamp"
checkbox also includes the timestamp of the sensor event as the
first field. The Start (NDK) button starts recording using NDK
C++ code (it probably makes no difference compared to Java -
implementing in NDK was just an experiment with using sensors
from the NDK).


