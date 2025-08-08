// gstgainbp.c - GStreamer audio filter: gain + band-pass (HPF+LPF)
// Build example (pkg-config must find gstreamer-1.0 and gstreamer-audio-1.0):
//   gcc -fPIC -shared -o libgstgainbp.so gstgainbp.c $(pkg-config --cflags --libs gstreamer-1.0 gstreamer-audio-1.0) -lm
// Or with your Makefile (produces libgstgainbp.so)

#include <gst/gst.h>
#include <gst/audio/audio.h>
#include <gst/audio/gstaudiofilter.h>
#include <math.h>
#include <string.h>
#ifndef PACKAGE
#define PACKAGE "gainbp"
#endif

#define GST_TYPE_GAINBP   (gst_gainbp_get_type())
G_DECLARE_FINAL_TYPE (GstGainBP, gst_gainbp, GST, GAINBP, GstAudioFilter)

typedef struct {
  float b0, b1, b2, a1, a2;   // a0 normalized to 1
  float x1, x2, y1, y2;
} Biquad;

struct _GstGainBP {
  GstAudioFilter parent;

  gdouble gain;
  gdouble lowcut_hz;   // HPF cutoff
  gdouble highcut_hz;  // LPF cutoff

  gint rate;
  gint channels;

  gboolean coeffs_valid;

  Biquad *hpf; // per-channel
  Biquad *lpf; // per-channel
};

G_DEFINE_TYPE (GstGainBP, gst_gainbp, GST_TYPE_AUDIO_FILTER)

// ---------- Biquad helpers ----------
static void
biquad_reset (Biquad *bq, gint channels)
{
  for (int c = 0; c < channels; c++) {
    bq[c].x1 = bq[c].x2 = bq[c].y1 = bq[c].y2 = 0.0f;
  }
}

static void
design_lowpass (Biquad *bq, double fs, double fc, double Q)
{
  if (fc <= 0.0 || fc >= fs * 0.49) {
    bq->b0 = 1.0f; bq->b1 = 0.0f; bq->b2 = 0.0f; bq->a1 = 0.0f; bq->a2 = 0.0f;
    return;
  }
  double w0 = 2.0 * M_PI * fc / fs;
  double cw = cos(w0), sw = sin(w0);
  double alpha = sw / (2.0 * Q);

  double b0 = (1.0 - cw) * 0.5;
  double b1 = 1.0 - cw;
  double b2 = (1.0 - cw) * 0.5;
  double a0 = 1.0 + alpha;
  double a1 = -2.0 * cw;
  double a2 = 1.0 - alpha;

  bq->b0 = (float)(b0 / a0);
  bq->b1 = (float)(b1 / a0);
  bq->b2 = (float)(b2 / a0);
  bq->a1 = (float)(a1 / a0);
  bq->a2 = (float)(a2 / a0);
}

static void
design_highpass (Biquad *bq, double fs, double fc, double Q)
{
  if (fc <= 0.0 || fc >= fs * 0.49) {
    bq->b0 = 1.0f; bq->b1 = 0.0f; bq->b2 = 0.0f; bq->a1 = 0.0f; bq->a2 = 0.0f;
    return;
  }
  double w0 = 2.0 * M_PI * fc / fs;
  double cw = cos(w0), sw = sin(w0);
  double alpha = sw / (2.0 * Q);

  double b0 =  (1.0 + cw) * 0.5;
  double b1 = -(1.0 + cw);
  double b2 =  (1.0 + cw) * 0.5;
  double a0 =  1.0 + alpha;
  double a1 = -2.0 * cw;
  double a2 =  1.0 - alpha;

  bq->b0 = (float)(b0 / a0);
  bq->b1 = (float)(b1 / a0);
  bq->b2 = (float)(b2 / a0);
  bq->a1 = (float)(a1 / a0);
  bq->a2 = (float)(a2 / a0);
}

static inline float
biquad_process (Biquad *s, float x)
{
  float y = s->b0 * x + s->b1 * s->x1 + s->b2 * s->x2 - s->a1 * s->y1 - s->a2 * s->y2;
  s->x2 = s->x1;
  s->x1 = x;
  s->y2 = s->y1;
  s->y1 = y;
  return y;
}

// ---------- GObject properties ----------
enum {
  PROP_0,
  PROP_GAIN,
  PROP_LOWCUT,
  PROP_HIGHCUT,
};

static void
gst_gainbp_set_property (GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec)
{
  GstGainBP *self = GST_GAINBP (object);

  switch (prop_id) {
    case PROP_GAIN:
      self->gain = g_value_get_double (value);
      break;
    case PROP_LOWCUT:
      self->lowcut_hz = g_value_get_double (value);
      self->coeffs_valid = FALSE;
      break;
    case PROP_HIGHCUT:
      self->highcut_hz = g_value_get_double (value);
      self->coeffs_valid = FALSE;
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
  }
}

static void
gst_gainbp_get_property (GObject *object, guint prop_id, GValue *value, GParamSpec *pspec)
{
  GstGainBP *self = GST_GAINBP (object);

  switch (prop_id) {
    case PROP_GAIN:
      g_value_set_double (value, self->gain);
      break;
    case PROP_LOWCUT:
      g_value_set_double (value, self->lowcut_hz);
      break;
    case PROP_HIGHCUT:
      g_value_set_double (value, self->highcut_hz);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
  }
}

// ---------- Filter design ----------
static void redesign_filters (GstGainBP *self)
{
  if (self->rate <= 0 || self->channels <= 0)
    return;

  if (!self->hpf) self->hpf = g_new0 (Biquad, self->channels);
  if (!self->lpf) self->lpf = g_new0 (Biquad, self->channels);

  const double fs = (double) self->rate;
  const double Q = 1.0 / sqrt (2.0); // ~Butterworth

  double low = self->lowcut_hz;
  double high = self->highcut_hz;
  if (low < 0.0) low = 0.0;
  if (high < 0.0) high = 0.0;
  if (high > fs * 0.49) high = fs * 0.49;
  if (high > 0.0 && low >= high) low = high * 0.8;

  Biquad hpf, lpf;
  design_highpass (&hpf, fs, low, Q);
  design_lowpass  (&lpf, fs, high, Q);

  for (int c = 0; c < self->channels; c++) {
    self->hpf[c] = hpf;
    self->lpf[c] = lpf;
  }
  biquad_reset (self->hpf, self->channels);
  biquad_reset (self->lpf, self->channels);

  self->coeffs_valid = TRUE;

  GST_DEBUG_OBJECT (self, "redesign: fs=%d, ch=%d, low=%.2f, high=%.2f",
                    self->rate, self->channels, low, high);
}

// ---------- GstAudioFilter / BaseTransform ----------
static gboolean
gst_gainbp_setup (GstAudioFilter *filter, const GstAudioInfo *info)
{
  GstGainBP *self = GST_GAINBP (filter);

  self->rate = GST_AUDIO_INFO_RATE (info);
  self->channels = GST_AUDIO_INFO_CHANNELS (info);

  if (GST_AUDIO_INFO_FORMAT (info) != GST_AUDIO_FORMAT_F32LE) {
    GST_ERROR_OBJECT (self, "Unsupported format (expected F32LE)");
    return FALSE;
  }

  self->coeffs_valid = FALSE;
  return TRUE;
}

static GstFlowReturn
gst_gainbp_transform_ip (GstBaseTransform *base, GstBuffer *buf)
{
  GstGainBP *self = GST_GAINBP (base);

  if (!self->coeffs_valid)
    redesign_filters (self);

  GstMapInfo map;
  if (!gst_buffer_map (buf, &map, GST_MAP_READWRITE))
    return GST_FLOW_ERROR;

  gint channels = self->channels;
  gint n_samples_total = (gint)(map.size / sizeof(float));
  if (channels <= 0 || (n_samples_total % channels) != 0) {
    gst_buffer_unmap (buf, &map);
    return GST_FLOW_ERROR;
  }
  gint n_frames = n_samples_total / channels;
  float *data = (float *) map.data;

  for (int i = 0; i < n_frames; i++) {
    for (int ch = 0; ch < channels; ch++) {
      int idx = i * channels + ch;
      float x = data[idx];
      float y = biquad_process (&self->hpf[ch], x);
      y = biquad_process (&self->lpf[ch], y);
      y *= (float) self->gain;
      data[idx] = y;
    }
  }

  gst_buffer_unmap (buf, &map);
  return GST_FLOW_OK;
}

static gboolean
gst_gainbp_start (GstBaseTransform *base)
{
  GstGainBP *self = GST_GAINBP (base);
  self->coeffs_valid = FALSE;
  return TRUE;
}

static gboolean
gst_gainbp_stop (GstBaseTransform *base)
{
  GstGainBP *self = GST_GAINBP (base);
  g_clear_pointer (&self->hpf, g_free);
  g_clear_pointer (&self->lpf, g_free);
  return TRUE;
}

static void
gst_gainbp_finalize (GObject *obj)
{
  GstGainBP *self = GST_GAINBP (obj);
  g_clear_pointer (&self->hpf, g_free);
  g_clear_pointer (&self->lpf, g_free);
  G_OBJECT_CLASS (gst_gainbp_parent_class)->finalize (obj);
}

// ---------- Boilerplate ----------
static void
gst_gainbp_class_init (GstGainBPClass *klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  GstBaseTransformClass *bt_class = GST_BASE_TRANSFORM_CLASS (klass);
  GstAudioFilterClass *af_class = GST_AUDIO_FILTER_CLASS (klass);

  gobject_class->set_property = gst_gainbp_set_property;
  gobject_class->get_property = gst_gainbp_get_property;
  gobject_class->finalize = gst_gainbp_finalize;

  g_object_class_install_property (
      gobject_class, PROP_GAIN,
      g_param_spec_double ("gain", "Gain",
                           "Linear gain factor (1.0 = unity, 2.0 = +6 dB)",
                           0.0, 64.0, 1.0,
                           G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (
      gobject_class, PROP_LOWCUT,
      g_param_spec_double ("lowcut", "Lowcut (HPF) Hz",
                           "High-pass cutoff frequency (Hz)",
                           0.0, 192000.0, 100.0,
                           G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (
      gobject_class, PROP_HIGHCUT,
      g_param_spec_double ("highcut", "Highcut (LPF) Hz",
                           "Low-pass cutoff frequency (Hz)",
                           0.0, 192000.0, 8000.0,
                           G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  bt_class->start = gst_gainbp_start;
  bt_class->stop  = gst_gainbp_stop;
  bt_class->transform_ip = gst_gainbp_transform_ip;

  af_class->setup = gst_gainbp_setup;

  // Pads: F32LE, 1..8 ch, 1..192kHz
  GstCaps *caps = gst_caps_new_simple ("audio/x-raw",
                                       "format", G_TYPE_STRING, "F32LE",
                                       "rate",   GST_TYPE_INT_RANGE, 1, 192000,
                                       "channels", GST_TYPE_INT_RANGE, 1, 8,
                                       NULL);

  gst_element_class_set_static_metadata (GST_ELEMENT_CLASS (klass),
    "Gain + Band-pass Filter", "Filter/Effect/Audio",
    "Applies high-pass + low-pass (band-pass) and gain to F32LE audio",
    "Manuel <you@example.com>");

  gst_audio_filter_class_add_pad_templates (af_class, caps);

  gst_caps_unref (caps);
}

static void
gst_gainbp_init (GstGainBP *self)
{
  self->gain = 1.0;
  self->lowcut_hz = 100.0;
  self->highcut_hz = 8000.0;
  self->rate = 0;
  self->channels = 0;
  self->coeffs_valid = FALSE;
  self->hpf = NULL;
  self->lpf = NULL;

  gst_base_transform_set_in_place (GST_BASE_TRANSFORM (self), TRUE);
  gst_base_transform_set_passthrough (GST_BASE_TRANSFORM (self), FALSE);
}

static gboolean
plugin_init (GstPlugin *plugin)
{
  return gst_element_register (plugin, "gainbp", GST_RANK_NONE, GST_TYPE_GAINBP);
}

GST_PLUGIN_DEFINE (
    GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    gainbp,
    "Gain + Band-pass filter",
    plugin_init,
    "1.0",
    "LGPL",
    "gainbp",
    "https://example.com/"
)

