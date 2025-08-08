#Instructions

Execute the Terminal inside the gst-plugin-gain+bandpass folder

Run:
make clean
make
export GST_PLUGIN_PATH=$PWD:$GST_PLUGIN_PATH
gst-launch-1.0 -v \
  pulsesrc ! audioconvert ! audioresample \
  ! audio/x-raw,format=F32LE,rate=48000,channels=1 \
  ! gainbp gain=2 lowcut=120 highcut=3500 \
  ! autoaudiosink


You can update the gain, low and high cut frequencies. In order to have
better results use a headphone instead of the default laptop speakers
