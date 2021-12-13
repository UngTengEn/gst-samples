#include <stdio.h>
#include <gst/gst.h>
#include <gst/video/video-format.h>
#include <gst/video/video-event.h>

//gcc -g -Wall -pthread `pkg-config --cflags glib-2.0` `pkg-config --cflags gstreamer-video-1.0` dynamic_bitrates.c -o dynamic_bitrates `pkg-config --libs glib-2.0` `pkg-config --libs gstreamer-video-1.0`

static guint bitrates[] = {
    2048,
    8192,
    1024,
    4096,
    0
};

static guint i = 0;

static gboolean
change_bitrate(GstElement *encoder) {

    if (bitrates[i]) {
        g_object_set(encoder, "bitrate", bitrates[i], NULL);
        
        ++i;
        if (bitrates[i]) return TRUE;
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
    GstElement *pipeline, *src, *filter, *enc, *encformat, *parse, *mux, *sink;
    GstCaps *srccaps, *enccaps;
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
    encformat = gst_element_factory_make("capsfilter", "encformat");
    parse = gst_element_factory_make("h264parse", "parse");
    mux = gst_element_factory_make("mp4mux", "mux");
    sink = gst_element_factory_make("filesink", "sink");

    if (!pipeline || !src || !filter || !enc || !encformat || !parse || !mux || !sink) {
        g_printerr("At least one element could not be created :-\n");
	GST_OBJECT_FREE_AND_LOG(pipeline);
	GST_OBJECT_FREE_AND_LOG(src);
	GST_OBJECT_FREE_AND_LOG(filter);
	GST_OBJECT_FREE_AND_LOG(enc);
	GST_OBJECT_FREE_AND_LOG(encformat);
	GST_OBJECT_FREE_AND_LOG(parse);
	GST_OBJECT_FREE_AND_LOG(mux);
	GST_OBJECT_FREE_AND_LOG(sink);
        g_printerr("Exiting...\n");

        return -1;
    }

    /* Set up the componets needed on the pipeline */
    g_object_set(src, "num-buffers", 800, NULL);

    srccaps = gst_caps_from_string("video/x-raw,format=YUY2, width=1920,height=1080,framerate=25/1");
    g_object_set(filter, "caps", srccaps, NULL);
    gst_caps_unref(srccaps);

    g_object_set(enc, "bitrate", 64, NULL);

    enccaps = gst_caps_from_string("video/x-h264, stream-format=byte-stream, profile=main");
    g_object_set(encformat, "caps", enccaps, NULL);
    gst_caps_unref(enccaps);

    g_object_set(sink, "location", "./dynamic_bitrates.mp4", NULL);

    /* Add a message handler */
    bus = gst_element_get_bus(pipeline);
    bus_watch_id = gst_bus_add_watch(bus, bus_call, loop);
    g_object_unref(bus);

    /* Add all elements into the pipeline */
    gst_bin_add_many(GST_BIN(pipeline), src, filter, enc, encformat, parse, mux, sink, NULL);

    /* link elements together */
    gst_element_link_many(src, filter, enc, encformat, parse, mux, sink, NULL);

    /* Set state to playing */
    gst_element_set_state(GST_ELEMENT(pipeline), GST_STATE_PLAYING);

    /* Change bitrate in call back */
    g_timeout_add(1500, (GSourceFunc) change_bitrate, enc);

    /* Iterate, running */
    g_main_loop_run(loop);

    /* Returned, stopping playback */
    gst_element_set_state(GST_ELEMENT(pipeline), GST_STATE_NULL);

    gst_object_unref(GST_OBJECT(pipeline));
    g_source_remove(bus_watch_id);
    g_main_loop_unref(loop);

    return 0;
}
