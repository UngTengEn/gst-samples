#include <stdio.h>
#include <gst/gst.h>
#include <gst/video/video-format.h>
#include <gst/video/video-event.h>

//gcc -g -Wall -pthread `pkg-config --cflags glib-2.0` `pkg-config --cflags gstreamer-video-1.0` dynamic_framesizes.c -o dynamic_framesizes `pkg-config --libs glib-2.0` `pkg-config --libs gstreamer-video-1.0`

static gchar* caps[] = {
    "video/x-raw,format=YUY2, width=1600,height=1200,framerate=25/1",
    "video/x-raw,format=YUY2, width=1600,height=900,framerate=25/1",
    "video/x-raw,format=YUY2, width=1280,height=1024,framerate=25/1",
    "video/x-raw,format=YUY2, width=1280,height=720,framerate=25/1",
    NULL
};

static guint capsindex = 0;

static gboolean
change_framesize(GstElement *filter) {
    GstCaps *srccaps;

    if (caps[capsindex]) {
        srccaps = gst_caps_from_string(caps[capsindex]);
        g_object_set(filter, "caps", srccaps, NULL);
        gst_caps_unref(srccaps);
        
        ++capsindex;
        if (caps[capsindex]) return TRUE;
    }

    return FALSE;
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
    GstElement *pipeline, *src, *filter, *enc, *dec, *sink;
    GstCaps *srccaps;
    GMainLoop *loop;
    GstBus *bus;
    guint bus_watch_id; 

    /* init */
    gst_init(&argc, &argv);

    loop = g_main_loop_new(NULL, FALSE);

    /* create pipeline */
    pipeline = gst_pipeline_new("my-pipeline");

    /* create elements */
    src = gst_element_factory_make("videotestsrc", "src");
    filter = gst_element_factory_make("capsfilter", "srccaps");
    enc = gst_element_factory_make("msdkh264enc", "enc");
    dec = gst_element_factory_make("msdkh264dec", "dec");
    sink = gst_element_factory_make("glimagesink", "sink");

    if (!pipeline || !src || !filter || !enc || !dec || !sink) {
        g_printerr("At least one element could not be created :-\n");
        GST_OBJECT_FREE_AND_LOG(pipeline);
        GST_OBJECT_FREE_AND_LOG(src);
        GST_OBJECT_FREE_AND_LOG(filter);
        GST_OBJECT_FREE_AND_LOG(enc);
        GST_OBJECT_FREE_AND_LOG(dec);
        GST_OBJECT_FREE_AND_LOG(sink);
        g_printerr("Exiting...\n");

        return -1;
    }

    /* Set up the componets needed on the pipeline */
    g_object_set(src, "num-buffers", 800, NULL);

    srccaps = gst_caps_from_string("video/x-raw,format=YUY2, width=1920,height=1080,framerate=25/1");
    g_object_set(filter, "caps", srccaps, NULL);
    gst_caps_unref(srccaps);

    /* Add a message handler */
    bus = gst_element_get_bus(pipeline);
    bus_watch_id = gst_bus_add_watch(bus, bus_call, loop);
    g_object_unref(bus);

    /* Add all elements into the pipeline */
    gst_bin_add_many(GST_BIN(pipeline), src, filter, enc, dec, sink, NULL);

    /* link elements together */
    gst_element_link_many(src, filter, enc, dec, sink, NULL);

    /* Set state to playing */
    gst_element_set_state(GST_ELEMENT(pipeline), GST_STATE_PLAYING);

    /* Change frame size in call back */
    g_timeout_add(5000, (GSourceFunc) change_framesize, filter);

    /* Iterate, running */
    g_main_loop_run(loop);

    /* Returned, stopping playback */
    gst_element_set_state(GST_ELEMENT(pipeline), GST_STATE_NULL);

    gst_object_unref(GST_OBJECT(pipeline));
    g_source_remove(bus_watch_id);
    g_main_loop_unref(loop);

    return 0;
}
