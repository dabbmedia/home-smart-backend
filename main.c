//
//  camera.cpp
//  home_smart
//
//  Created by Brent Self on 6/26/20.
//  Copyright © 2020 Brent Self. All rights reserved.
//
#include <sys/stat.h>
#include <gst/gst.h>
#include <gst/audio/audio.h>
#include <string.h>
#include <stdio.h>

/* Structure to contain all our information, so we can pass it to callbacks */
typedef struct _CameraData {
    GstElement *pipeline, *video_source, *filter, *filter_h264, *video_convert, *h264_encode, *h264_parse, *mpegts_mux, *hls_sink;
    // GstElement *clock_overlay, *image_queue, *multifile_sink;
    GstElement *tee, *video_queue;
    GstElement *app_queue, *rawvideo_parse, *app_sink, *fake_sink;

    GstBuffer *buffer, *previous_buffer;

    GstClockTime *motion_ts_start;

    guint64 num_samples;   /* Number of samples generated so far (for timestamp generation) */

    gint video_width, video_height, tolerance, tolerance_r, tolerance_g, tolerance_b, motion_buffer_count;

    gboolean is_live;

    gboolean has_native_h264;

    GMainLoop *loop;  /* GLib's Main Loop */
} CameraData;

void *SaveVideoClip(void *threadid) {
  long tid;
  tid = (long)threadid;
  printf("Hello World! Thread ID, %d\n", tid);
  // int a = system("cat segment1_0_av.ts segment2_0_av.ts segment3_0_av.ts > all.ts; ffmpeg -i all.ts -acodec copy -vcodec copy all.mp4");
  pthread_exit(NULL);
}

static void compare_rgb_buffers (CameraData *data) {
  GstMapInfo map, previous_map;

  if (gst_buffer_map (data->buffer, &map, GST_MAP_READ) && gst_buffer_map (data->previous_buffer, &previous_map, GST_MAP_READ)) {
    const guchar * mem = map.data;
    guint size = map.size;
    const guchar * mem_2 = previous_map.data;
    guint size_2 = previous_map.size;
    if (size == size_2) {
      guint i;
      // guint rstride = data->video_width * 4;

      i = 0;
      while (i < size) {
        // i++;
        // j++;
        i += 16;
        // j += 4;

        // if (j < 4) {
          data->tolerance_r = abs(mem[i] - mem_2[i]) - data->tolerance;
          data->tolerance_g = abs(mem[i+1] - mem_2[i+1]) - data->tolerance;
          data->tolerance_b = abs(mem[i+2] - mem_2[i+2]) - data->tolerance;
          if (data->tolerance_r > 0 || data->tolerance_g > 0 || data->tolerance_b > 0) {
            // g_print("motion detected, cur: %d, prev: %d, diff: %d, diff_tol: %d\n", mem[i], mem_2[i], abs(mem[i] - mem_2[i]), abs_tolerance);
            g_print("motion detected (%d): %d, %d, %d, ", data->tolerance, data->tolerance_r, data->tolerance_g, data->tolerance_b);
            if (data->buffer->pts != GST_CLOCK_TIME_NONE) {
              data->motion_ts_start = data->buffer->pts;
              printf("%" GST_TIME_FORMAT "\n", GST_TIME_ARGS(data->motion_ts_start));
              // create video clip from mpegts queue
              // copy clips from /tmp/hls to permanent storage and splice, if necessary
              pthread_t threads[1];
              int rc;
              rc = pthread_create(&threads[i], NULL, SaveVideoClip, (void *)0);
              if (rc) {
                 printf("Error:unable to create thread, %d\n", rc);
                 exit(-1);
              }
            }
            // log motion event in DB
            // send email or API call to send email
            // system ("echo 'Motion Detected from Pi4 camera' | mail -s Motion Detected brentself@gmail.com");
            break;
          }
        // }
        // if (j == 4 || i == size) {
        //   j = 0;
        // }
      }
    }
  }
  gst_buffer_unmap(data->buffer, &map);
  gst_buffer_unmap(data->previous_buffer, &previous_map);
  // gst_buffer_unref(data->buffer);
}

GstFlowReturn new_sample (GstElement *sink, CameraData *data) {
  GstSample *sample;
  GstBuffer *buffer;
  // GstMapInfo map;
  /* Retrieve the buffer */
  g_signal_emit_by_name (sink, "pull-sample", &sample);
  if (GST_IS_SAMPLE(sample)) {
    buffer = gst_sample_get_buffer(sample);
    if (buffer != NULL) {
      data->buffer = gst_buffer_copy_deep(buffer);
      if (GST_IS_BUFFER(data->buffer) && GST_IS_BUFFER(data->previous_buffer)) {
        compare_rgb_buffers (data);
        gst_buffer_unref(data->previous_buffer);
      }
      data->previous_buffer = data->buffer;
    }
    
    gst_sample_unref (sample);
    return GST_FLOW_OK;
  }

  return GST_FLOW_ERROR;
}

int main(int argc, char *argv[]) {
    g_printerr("Camera object created.\n");
    
    struct stat st = {0};
    char jpg_dir[] = "/tmp/jpg";
    char hls_dir[] = "/tmp/hls";

    if (stat(jpg_dir, &st) == -1) {
        mkdir(jpg_dir, 0766);
    }
    if (stat(hls_dir, &st) == -1) {
        mkdir(hls_dir, 0766);
    }

    GstPad *tee_video_pad, *tee_motion_pad, *tee_app_pad; //*tee_audio_pad,
    GstPad *queue_video_pad, *queue_motion_pad, *queue_app_pad; //*queue_audio_pad,
    GstBus *bus;
    guint bus_watch_id;

    CameraData data;
    memset (&data, 0, sizeof (data));
    data.video_width = 640;
    data.video_height = 480;
    data.motion_buffer_count = 0;
    data.tolerance = 40;

    /* Initialize GStreamer */
    gst_init (&argc, &argv);

    /* Create the elements */
  data.tee = gst_element_factory_make ("tee", "tee");
  
  data.video_queue = gst_element_factory_make ("queue", "video_queue");
  
  #if defined(__unix__) || defined(__linux__)
    data.video_source = gst_element_factory_make("v4l2src", "video_source");
    g_object_set (data.video_source, "device", "/dev/video0", NULL);
    g_object_set (data.video_source, "num-buffers", -1, NULL);
  #elif defined(_WIN32) || defined(WIN32)
    #include <windows.h>
    data.video_source = gst_element_factory_make("ksvideosrc", "video_source");
    g_object_set (data.video_source, "do-stats", TRUE, NULL);
  #endif
  
  // this->get_camera_filter (data);
  GstCaps *filtercaps = NULL;
  data.filter = gst_element_factory_make ("capsfilter", "filter");
  filtercaps = gst_caps_new_simple ("video/x-raw",
    "framerate", GST_TYPE_FRACTION, 25, 1,
    "width", G_TYPE_INT, data.video_width,
    "height", G_TYPE_INT, data.video_height,
    NULL);
  g_object_set (G_OBJECT (data.filter), "caps", filtercaps, NULL);
  gst_caps_unref (filtercaps);
  
  data.h264_encode = gst_element_factory_make ("v4l2h264enc", "h264_encode"); // omxh264enc
  
  data.h264_parse = gst_element_factory_make ("h264parse", "h264_parse");
  // required to play in safari, without only plays in VLC
  g_object_set (data.h264_parse, "config-interval", -1, NULL);

  data.mpegts_mux = gst_element_factory_make ("mpegtsmux", "mpegts_mux");
  
  data.hls_sink = gst_element_factory_make ("hlssink", "hls_sink");
  g_object_set (data.hls_sink, "location", "/tmp/hls/segment%05d.ts", NULL);
  g_object_set (data.hls_sink, "playlist-location", "/tmp/hls/playlist.m3u8", NULL);
  g_object_set (data.hls_sink, "max-files", 150, NULL);
  g_object_set (data.hls_sink, "target-duration", 2, NULL);
  g_object_set (data.hls_sink, "playlist-length", 60, NULL);

  data.app_queue = gst_element_factory_make ("queue", "app_queue");

  // data.rawvideo_parse = gst_element_factory_make ("rawvideoparse", "rawvideo_parse");
  // g_object_set (data.rawvideo_parse, "format", 7, NULL); // GST_VIDEO_FORMAT_RGBx
  // g_object_set (data.rawvideo_parse, "width", data.video_width, NULL);
  // g_object_set (data.rawvideo_parse, "height", data.video_height, NULL);
  // g_object_set (data.rawvideo_parse, "framerate", 1, 1, NULL);

  data.app_sink = gst_element_factory_make ("appsink", "app_sink");
  // g_object_set (data.app_sink, "max-buffers", 30, NULL);
  // g_object_set (data.app_sink, "drop", TRUE, NULL);
  // g_object_set (data.app_sink, "throttle-time", 1, NULL);
  // g_object_set (data.app_sink, "sync", FALSE, NULL);
  // GstCaps *appcaps = NULL;
  // appcaps = gst_caps_new_simple ("video/x-raw",
  //   "framerate", GST_TYPE_FRACTION, 1, 1,
  //   "width", G_TYPE_INT, data.video_width,
  //   "height", G_TYPE_INT, data.video_height,
  //   NULL);
  // g_object_set (G_OBJECT (data.app_sink), "caps", appcaps, NULL);
  g_object_set (data.app_sink, "emit-signals", TRUE, NULL);
  g_signal_connect (data.app_sink, "new-sample", G_CALLBACK (new_sample), &data);

  /* Create the empty pipeline */
  data.pipeline = gst_pipeline_new ("home_smart_camera-pipeline");

  if (!data.pipeline) {
  g_printerr ("Pipeline element could not be created.\n");
  return -1;
  }
  if (!data.video_source) {
  g_printerr ("video_source element could not be created.\n");
  return -1;
  }
  if (!data.filter) {
  g_printerr ("filter element could not be created.\n");
  return -1;
  }
  if (data.has_native_h264) {
    if (!data.filter_h264) {
    g_printerr ("filter_h264 element could not be created.\n");
    return -1;
    }
  }
  if (!data.tee) {
  g_printerr ("tee element could not be created.\n");
  return -1;
  }
  if (!data.video_queue) {
  g_printerr ("video_queue element could not be created.\n");
  return -1;
  }
  if (!data.h264_encode && !data.has_native_h264) {
  g_printerr ("h264_encode element could not be created.\n");
  return -1;
  }
  if (!data.h264_parse) {
  g_printerr ("h264_parse element could not be created.\n");
  return -1;
  }
  if (!data.mpegts_mux) {
  g_printerr ("mpegts_mux element could not be created.\n");
  return -1;
  }
  // if (!data.video_convert) {
  // g_printerr ("video_convert element could not be created.\n");
  // return -1;
  // }
  if (!data.hls_sink) {
  g_printerr ("hls_sink element could not be created.\n");
  return -1;
  }
  if (!data.app_queue) {
  g_printerr ("app_queue element could not be created.\n");
  return -1;
  }
  // if (!data.rawvideo_parse) {
  // g_printerr ("rawvideo_parse element could not be created.\n");
  // return -1;
  // }
  if (!data.app_sink) {
  g_printerr ("app_sink element could not be created.\n");
  return -1;
  }

  /* Link all elements that can be automatically linked because they have "Always" pads */
  GstBin *gstbin = GST_BIN (data.pipeline);
  gst_bin_add_many (gstbin, 
                    data.video_source, 
                    data.filter, 
                    data.tee, 
                    data.video_queue, 
                    data.h264_encode, 
                    data.h264_parse, 
                    data.mpegts_mux, 
                    data.hls_sink, 
                    data.app_queue, 
                    // data.rawvideo_parse,
                    data.app_sink, 
                    NULL);
  
  if (gst_element_link_many (data.video_source, 
                            data.filter, 
                            data.tee, 
                            NULL) != TRUE) {
      g_printerr ("v4l2src, filter and tee elements could not be linked.\n");
      gst_object_unref (data.pipeline);
      return -1;
  }
  if (gst_element_link_many (data.video_queue, 
                            data.h264_encode, 
                            data.h264_parse, 
                            data.mpegts_mux, 
                            data.hls_sink, 
                            NULL) != TRUE) {
      g_printerr ("Video queue elements could not be linked.\n");
      gst_object_unref (data.pipeline);
      return -1;
  }
  if (gst_element_link_many (data.app_queue, 
                            // data.rawvideo_parse, 
                            data.app_sink, 
                            NULL) != TRUE) {
      g_printerr ("Data elements could not be linked.\n");
      gst_object_unref (data.pipeline);
      return -1;
  }
  // for command line testing of the pipeline
// sudo gst-launch-1.0 -v v4l2src device=/dev/video0 ! video/x-raw,width=640,height=480,framerate=30/1 ! videoconvert ! v4l2h264enc ! h264parse config-interval=-1 ! mpegtsmux ! hlssink target-duration=1 location="/tmp/hls/segment%05d.ts" playlist-location="/tmp/hls/playlist.m3u8"
// sudo gst-launch-1.0 -v v4l2src device=/dev/video0 ! video/x-h264,width=640,height=480,framerate=30/1 ! h264parse config-interval=-1 ! mpegtsmux ! hlssink target-duration=1 location="/tmp/hls/segment%05d.ts" playlist-location="/tmp/hls/playlist.m3u8"
  /* Manually link the Tee, which has "Request" pads */
  tee_video_pad = gst_element_get_request_pad (data.tee, "src_%u");
  g_printerr ("Obtained request pad %s for video branch.\n", gst_pad_get_name (tee_video_pad));
  queue_video_pad = gst_element_get_static_pad (data.video_queue, "sink");
  
  tee_app_pad = gst_element_get_request_pad (data.tee, "src_%u");
  g_printerr ("Obtained request pad %s for app branch.\n", gst_pad_get_name (tee_app_pad));
  queue_app_pad = gst_element_get_static_pad (data.app_queue, "sink");
  
  if (gst_pad_link (tee_video_pad, queue_video_pad) != GST_PAD_LINK_OK) {
      g_printerr ("Tee video pad could not be linked\n");
      gst_object_unref (data.pipeline);
      return -1;
  }
  if (gst_pad_link (tee_app_pad, queue_app_pad) != GST_PAD_LINK_OK) {
      g_printerr ("Tee app pad could not be linked\n");
      gst_object_unref (data.pipeline);
      return -1;
  }
  gst_object_unref (queue_video_pad);
  gst_object_unref (queue_app_pad);

  // generate pipeline graph file
  gst_debug_bin_to_dot_file(gstbin, GST_DEBUG_GRAPH_SHOW_ALL, "pipeline");

  /* Instruct the bus to emit signals for each received message, and connect to the interesting signals */
  bus = gst_element_get_bus (data.pipeline);
  gst_bus_add_signal_watch (bus);
  g_signal_connect (bus, "message", (GCallback)message_cb, &data);
  // bus_watch_id = gst_bus_add_watch (bus, motion_cb, NULL);
  gst_object_unref (bus);

  /* Start playing the pipeline */
  gst_element_set_state (data.pipeline, GST_STATE_PLAYING);

  /* Create a GLib Main Loop and set it to run */
  data.loop = g_main_loop_new (NULL, FALSE);
  g_main_loop_run (data.loop);

  /* Release the request pads from the Tee, and unref them */
  gst_element_release_request_pad (data.tee, tee_video_pad);
  gst_element_release_request_pad (data.tee, tee_app_pad);
  gst_object_unref (tee_video_pad);
  gst_object_unref (tee_app_pad);

  /* Free resources */
  gst_element_set_state (data.pipeline, GST_STATE_NULL);
  gst_object_unref (data.pipeline);
  return 0;
}

static void cb_message (GstBus *bus, GstMessage *msg, CameraData *data) {

  switch (GST_MESSAGE_TYPE (msg)) {
    case GST_MESSAGE_ERROR: {
      GError *err;
      gchar *debug;

      gst_message_parse_error (msg, &err, &debug);
      g_print ("Error: %s\n", err->message);
      g_error_free (err);
      g_free (debug);

      gst_element_set_state (data->pipeline, GST_STATE_READY);
      g_main_loop_quit (data->loop);
      break;
    }
    case GST_MESSAGE_EOS:
      /* end-of-stream */
      gst_element_set_state (data->pipeline, GST_STATE_READY);
      g_main_loop_quit (data->loop);
      break;
    case GST_MESSAGE_BUFFERING: {
      gint percent = 0;

      /* If the stream is live, we do not care about buffering. */
      if (data->is_live) break;

      gst_message_parse_buffering (msg, &percent);
      g_print ("Buffering (%3d%%)\r", percent);
      /* Wait until buffering is complete before start/resume playing */
      if (percent < 100)
        gst_element_set_state (data->pipeline, GST_STATE_PAUSED);
      else
        gst_element_set_state (data->pipeline, GST_STATE_PLAYING);
      break;
    }
    case GST_MESSAGE_CLOCK_LOST:
      /* Get a new clock */
      gst_element_set_state (data->pipeline, GST_STATE_PAUSED);
      gst_element_set_state (data->pipeline, GST_STATE_PLAYING);
      break;
    default:
      /* Unhandled message */
      break;
    }
}

