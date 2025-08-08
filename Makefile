PLUGIN_NAME=gstgainbp
CFLAGS=`pkg-config --cflags gstreamer-1.0 gstreamer-audio-1.0`
LIBS=`pkg-config --libs gstreamer-1.0 gstreamer-audio-1.0`
CC=gcc

all: $(PLUGIN_NAME).so

$(PLUGIN_NAME).so: $(PLUGIN_NAME).c
	$(CC) -shared -fPIC $(PLUGIN_NAME).c -o lib$(PLUGIN_NAME).so $(CFLAGS) $(LIBS) -lm

clean:
	rm -f *.so
