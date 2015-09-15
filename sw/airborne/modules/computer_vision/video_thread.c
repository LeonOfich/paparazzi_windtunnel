/*
* Copyright (C) 2015
*
* This file is part of Paparazzi.
*
* Paparazzi is free software; you can redistribute it and/or modify
* it under the terms of the GNU General Public License as published by
* the Free Software Foundation; either version 2, or (at your option)
* any later version.
*
* Paparazzi is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
* GNU General Public License for more details.
*
* You should have received a copy of the GNU General Public License
* along with paparazzi; see the file COPYING.  If not, see
* <http://www.gnu.org/licenses/>.
*
*/

/**
 * @file modules/computer_vision/video_thread.c
 */

// Own header
#include "modules/computer_vision/video_thread.h"
#include "modules/computer_vision/cv.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/time.h>
#include <math.h>

// Video
#include "lib/v4l/v4l2.h"
#include "lib/vision/image.h"
#include "lib/encoding/jpeg.h"
#include "peripherals/video_device.h"

// include board for bottom_camera and front_camera on ARDrone2 and Bebop
#include BOARD_CONFIG

#if JPEG_WITH_EXIF_HEADER
#include "lib/exif/exif_module.h"
#endif

// Threaded computer vision
#include <pthread.h>
#include "rt_priority.h"

/// The camera video config (usually bottom_camera or front_camera)
#ifndef VIDEO_THREAD_CAMERA
#warning "Are you sure you don't want to use the bottom_camera or front_camera?"
// The video device buffers (the amount of V4L2 buffers)
#ifndef VIDEO_THREAD_DEVICE_BUFFERS
#define VIDEO_THREAD_DEVICE_BUFFERS 10
#endif
PRINT_CONFIG_VAR(VIDEO_THREAD_DEVICE_BUFFERS)
#ifndef VIDEO_THREAD_SUBDEV
#define VIDEO_THREAD_SUBDEV NULL
#endif
#ifndef VIDEO_THREAD_FILTERS
#define VIDEO_THREAD_FILTERS NULL
#endif
struct video_config_t custom_camera = {
  .w = VIDEO_THREAD_VIDEO_WIDTH,
  .h = VIDEO_THREAD_VIDEO_HEIGHT,
  .dev_name = STRINGIFY(VIDEO_THREAD_DEVICE),
  .subdev_name = VIDEO_THREAD_SUBDEV,
  .buf_cnt = VIDEO_THREAD_DEVICE_BUFFERS,
  .filters = VIDEO_THREAD_FILTERS
};
#define VIDEO_THREAD_CAMERA custom_camera
#endif
PRINT_CONFIG_VAR(VIDEO_THREAD_CAMERA)


// Frames Per Seconds
#ifndef VIDEO_THREAD_FPS
#define VIDEO_THREAD_FPS 4
#endif
PRINT_CONFIG_VAR(VIDEO_THREAD_FPS)

// The place where the shots are saved (without slash on the end)
#ifndef VIDEO_THREAD_SHOT_PATH
#define VIDEO_THREAD_SHOT_PATH "/data/video/images"
#endif
PRINT_CONFIG_VAR(VIDEO_THREAD_SHOT_PATH)

// Main thread
static void *video_thread_function(void *data);
void video_thread_periodic(void) { }

// Initialize the video_thread structure with the defaults
struct video_thread_t video_thread = {
  .is_running = FALSE,
  .fps = VIDEO_THREAD_FPS,
  .take_shot = FALSE,
  .shot_number = 0
};

/**
 * Handles all the video streaming and saving of the image shots
 * This is a sepereate thread, so it needs to be thread safe!
 */
static void *video_thread_function(void *data)
{

  // Start the streaming of the V4L2 device
  if (!v4l2_start_capture(video_thread.dev)) {
    printf("[video_thread-thread] Could not start capture of %s.\n", video_thread.dev->name);
    return 0;
  }

  // be nice to the more important stuff
  set_nice_level(10);

  // Initialize timing
  uint32_t microsleep = (uint32_t)(1000000. / (float)video_thread.fps);
  struct timeval last_time;
  gettimeofday(&last_time, NULL);

  // Start streaming
  video_thread.is_running = TRUE;
  while (video_thread.is_running) {
    // compute usleep to have a more stable frame rate
    struct timeval vision_thread_sleep_time;
    gettimeofday(&vision_thread_sleep_time, NULL);
    int dt = (int)(vision_thread_sleep_time.tv_sec - last_time.tv_sec) * 1000000 +
             (int)(vision_thread_sleep_time.tv_usec - last_time.tv_usec);
    if (dt < microsleep) { usleep(microsleep - dt); }
    last_time = vision_thread_sleep_time;

    // Wait for a new frame (blocking)
    struct image_t img;
    v4l2_image_get(video_thread.dev, &img);

    // Check if we need to take a shot
    if (video_thread.take_shot) {
      // Create a high quality image (99% JPEG encoded)
      struct image_t jpeg_hr;
      image_create(&jpeg_hr, img.w, img.h, IMAGE_JPEG);
      jpeg_encode_image(&img, &jpeg_hr, 99, TRUE);

      // Search for a file where we can write to
      char save_name[128];
      for (; video_thread.shot_number < 99999; video_thread.shot_number++) {
        sprintf(save_name, "%s/img_%05d.jpg", STRINGIFY(VIDEO_THREAD_SHOT_PATH), video_thread.shot_number);
        // Check if file exists or not
        if (access(save_name, F_OK) == -1) {
#if JPEG_WITH_EXIF_HEADER
          write_exif_jpeg(save_name, jpeg_hr.buf, jpeg_hr.buf_size, img.w, img.h);
#else
          FILE *fp = fopen(save_name, "w");
          if (fp == NULL) {
            printf("[video_thread-thread] Could not write shot %s.\n", save_name);
          } else {
            // Save it to the file and close it
            fwrite(jpeg_hr.buf, sizeof(uint8_t), jpeg_hr.buf_size, fp);
            fclose(fp);
          }
#endif
          // We don't need to seek for a next index anymore
          break;
        }
      }

      // We finished the shot
      image_free(&jpeg_hr);
      video_thread.take_shot = FALSE;
    }

    // Run processing if required
    cv_run(&img);

    // Free the image
    v4l2_image_free(video_thread.dev, &img);
  }

  return 0;
}

/**
 * Initialize the view video
 */
void video_thread_init(void)
{
  struct video_config_t *vid = (struct video_config_t *)&(VIDEO_THREAD_CAMERA);

  // Initialize the V4L2 subdevice if needed
  if (vid->subdev_name != NULL) {
    // FIXME! add subdev format to config, only needed on bebop front camera so far
    if (!v4l2_init_subdev(vid->subdev_name, 0, 0, V4L2_MBUS_FMT_SGBRG10_1X10, vid->w, vid->h)) {
      printf("[video_thread] Could not initialize the %s subdevice.\n", vid->subdev_name);
      return 0;
    }
  }

  // Initialize the V4L2 device
  video_thread.dev = v4l2_init(vid->dev_name, vid->w, vid->h, vid->buf_cnt, vid->format);
  if (video_thread.dev == NULL) {
    printf("[video_thread] Could not initialize the %s V4L2 device.\n", vid->dev_name);
    return 0;
  }

  // Create the shot directory
  char save_name[128];
  sprintf(save_name, "mkdir -p %s", STRINGIFY(VIDEO_THREAD_SHOT_PATH));
  if (system(save_name) != 0) {
    printf("[video_thread] Could not create shot directory %s.\n", STRINGIFY(VIDEO_THREAD_SHOT_PATH));
    return;
  }
}

/**
 * Start with streaming
 */
void video_thread_start(void)
{
  // Check if we are already running
  if (video_thread.is_running) {
    return;
  }

  // Start the streaming thread
  pthread_t tid;
  if (pthread_create(&tid, NULL, video_thread_function, NULL) != 0) {
    printf("[vievideo] Could not create streaming thread.\n");
    return;
  }
}

/**
 * Stops the streaming
 * This could take some time, because the thread is stopped asynchronous.
 */
void video_thread_stop(void)
{
  // Check if not already stopped streaming
  if (!video_thread.is_running) {
    return;
  }

  // Stop the streaming thread
  video_thread.is_running = FALSE;

  // Stop the capturing
  if (!v4l2_stop_capture(video_thread.dev)) {
    printf("[video_thread] Could not stop capture of %s.\n", video_thread.dev->name);
    return;
  }

  // TODO: wait for the thread to finish to be able to start the thread again!
}

/**
 * Take a shot and save it
 * This will only work when the streaming is enabled
 */
void video_thread_take_shot(bool_t take)
{
  video_thread.take_shot = take;
}
