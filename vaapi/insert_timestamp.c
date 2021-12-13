#include <stdio.h>
#include <gst/gst.h>
#include <gst/video/video-format.h>
#include <gst/video/video-event.h>
#include <gst/app/gstappsink.h>

//gcc -g -Wall -pthread `pkg-config --cflags glib-2.0` `pkg-config --cflags gstreamer-video-1.0` `pkg-config --cflags gstreamer-app-1.0` insert_timestamp.c -o insert_timestamp `pkg-config --libs glib-2.0` `pkg-config --libs gstreamer-video-1.0` `pkg-config --libs gstreamer-app-1.0`

#define METADATA_TIMESTAMP_CAP    "TimeStampMetaData"

static GstPadProbeReturn source_srcpad(GstPad *pad, GstPadProbeInfo *info, gpointer data)
{
    GstBuffer *buf = (GstBuffer *) info->data;

    static unsigned long fake_time = 0;
    fake_time++;
    GstCaps *ts_ref = gst_caps_new_empty_simple(METADATA_TIMESTAMP_CAP);
    gst_buffer_add_reference_timestamp_meta(buf, ts_ref, fake_time, GST_CLOCK_TIME_NONE);
    gst_caps_unref(ts_ref);

    printf( "using faketime=%lu\n", fake_time);
  
    return GST_PAD_PROBE_PASS;
}

static GstFlowReturn appsink_new_buffer(GstAppSink *appsink, gpointer data)
{
    GstSample *sample = gst_app_sink_pull_sample(appsink);
    if (!sample) {
        printf( "new_buffer_callback failed to get sample\n");
        return GST_FLOW_OK;
    }

    GstCaps *caps = gst_sample_get_caps(sample);
    if (caps) {
        GstBuffer *buffer = gst_sample_get_buffer(sample);
        GstMapInfo map    = { 0 };
        if (gst_buffer_map(buffer, &map, GST_MAP_READ)) {
            GstCaps *ts_ref = gst_caps_new_empty_simple(METADATA_TIMESTAMP_CAP);
            GstReferenceTimestampMeta *ts = gst_buffer_get_reference_timestamp_meta(buffer, ts_ref);
            gst_caps_unref(ts_ref);
            if (ts)
                printf( "appsink_new_buffer get timestamp=%lu\n", ts->timestamp);
            else
                printf( "appsink_new_buffer failed to get timestamp\n");
            gst_buffer_unmap(buffer, &map);
        }
    }
    gst_sample_unref( sample );

   return GST_FLOW_OK;
}

static gboolean
bus_call(GstBus * bus, GstMessage * msg, gpointer data)
{
    GMainLoop *loop = (GMainLoop *) data;

    switch (GST_MESSAGE_TYPE(msg)) {
    case GST_MESSAGE_EOS:{
        g_print("End-of-stream\n");
        g_main_loop_quit(loop);
        break;
    }

    default:
        break;
    }

    return TRUE;
}

static int
gst_object_check_and_free(GstObject *obj)
{
    if (obj) {
        gst_object_unref(obj);

        return 0;
    }

    return 1;
}

#define GST_OBJECT_FREE_AND_LOG(obj) \
    if (gst_object_check_and_free(GST_OBJECT(obj))) \
        g_printerr("   "#obj" is null\n")

int main(int argc, char *argv[])
{
    GstElement *pipeline, *src, *infilter, *tee, *q1, *q2, *vpp, *outfilter, *sink, *appsink;
    GstCaps *caps;
    GMainLoop *loop;
    GstBus *bus;
    GstPad *pad;
    guint bus_watch_id;

    /* init */
    gst_init(&argc, &argv);

    loop = g_main_loop_new(NULL, FALSE);

    /* create pipeline */
    pipeline = gst_pipeline_new("my-pipeline");

    /* create elements */
    src = gst_element_factory_make("videotestsrc", "src");
    infilter = gst_element_factory_make("capsfilter", "infilter");
    tee = gst_element_factory_make("tee", "tee");
    q1 = gst_element_factory_make("queue", "q1");
    q2 = gst_element_factory_make("queue", "q2");
    vpp = gst_element_factory_make("vaapipostproc", "vpp");
    outfilter = gst_element_factory_make("capsfilter", "outfilter");
    sink = gst_element_factory_make("fakesink", "sink");
    appsink = gst_element_factory_make("appsink", "appsink");

    if (!pipeline || !src || !infilter || !tee || !q1 || !q2 || !vpp || !outfilter || !sink || !appsink) {
        g_printerr("At least one element could not be created :-\n");
        GST_OBJECT_FREE_AND_LOG(pipeline);
        GST_OBJECT_FREE_AND_LOG(src);
        GST_OBJECT_FREE_AND_LOG(infilter);
        GST_OBJECT_FREE_AND_LOG(tee);
        GST_OBJECT_FREE_AND_LOG(q1);
        GST_OBJECT_FREE_AND_LOG(q2);
        GST_OBJECT_FREE_AND_LOG(vpp);
        GST_OBJECT_FREE_AND_LOG(outfilter);
        GST_OBJECT_FREE_AND_LOG(sink);
        GST_OBJECT_FREE_AND_LOG(appsink);
        g_printerr("Exiting...\n");

        return -1;
    }

    /* Set up the componets needed on the pipeline */
    g_object_set(src, "num-buffers", 100, NULL);

    caps = gst_caps_from_string("video/x-raw, format=NV12, width=1920, height=1080, framerate=25/1");
    g_object_set(infilter, "caps", caps, NULL);
    gst_caps_unref(caps);

//    caps = gst_caps_from_string("video/x-raw, width=1024, height=768, format=BGRA");
    caps = gst_caps_from_string("video/x-raw, width=800, height=600, format=BGRA");
    g_object_set(outfilter, "caps", caps, NULL);
    gst_caps_unref(caps);

    /* Add all elements into the pipeline */
    gst_bin_add_many(GST_BIN(pipeline), src, infilter, tee, q1, sink, q2, vpp, outfilter, appsink, NULL);

    /* link elements together */
    if (gst_element_link_many(src, infilter, tee, q1, sink, NULL) != TRUE) {
        g_printerr("Failed to link main queue\n");
        GST_OBJECT_FREE_AND_LOG(pipeline);

        return -1;
    }
    if (gst_element_link_many(tee, q2, vpp, outfilter, appsink, NULL) != TRUE) {
        g_printerr("Failed to link appsink queue\n");
        GST_OBJECT_FREE_AND_LOG(pipeline);

        return -1;
    }

    pad = gst_element_get_static_pad(src, "src");
    if (!pad) {
        g_printerr("Cannot find source src pad\n");
        GST_OBJECT_FREE_AND_LOG(pipeline);
        return -1;
    }
    gst_pad_add_probe(pad, GST_PAD_PROBE_TYPE_BUFFER, (GstPadProbeCallback) source_srcpad, NULL, NULL);
    gst_object_unref(pad);

    GstAppSinkCallbacks callbacks = { NULL, NULL, appsink_new_buffer };
    gst_app_sink_set_callbacks(GST_APP_SINK(appsink), &callbacks, NULL, NULL);

    /* Add a message handler */
    bus = gst_element_get_bus(pipeline);
    bus_watch_id = gst_bus_add_watch(bus, bus_call, loop);
    g_object_unref(bus);

    /* Set state to playing */
    gst_element_set_state(GST_ELEMENT(pipeline), GST_STATE_PLAYING);

    /* Iterate, running */
    g_main_loop_run(loop);

    /* Returned, stopping playback */
    gst_element_set_state(GST_ELEMENT(pipeline), GST_STATE_NULL);

    gst_object_unref(GST_OBJECT(pipeline));
    g_source_remove(bus_watch_id);
    g_main_loop_unref(loop);

    return 0;
}
