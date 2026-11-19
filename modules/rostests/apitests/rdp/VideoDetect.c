/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Test how the RDP console server finds the part of the desktop to send as video
 * COPYRIGHT:   Copyright 2026 mikuta0407 <mikuta0407@mikuta0407.net>
 */

#include <apitest.h>
#include "videodetect.h"

#define FRAME_MS 33

static BOOL Play(VIDEO_TRACKER *tracker, ULONGLONG *now, ULONGLONG duration, const RECT *rect)
{
    ULONGLONG end = *now + duration;
    BOOL active = FALSE;
    RECT ended;
    for (; *now < end; *now += FRAME_MS)
    {
        VideoTrackerChanged(tracker, *now, rect);
        active = VideoTrackerUpdate(tracker, *now, &ended);
    }
    return active;
}

static BOOL SameRect(const RECT *a, LONG left, LONG top, LONG right, LONG bottom)
{
    return a->left == left && a->top == top && a->right == right && a->bottom == bottom;
}

static void TestVideo(void)
{
    VIDEO_TRACKER tracker = {0};
    RECT video = { 100, 100, 420, 340 }, inside = { 128, 128, 192, 192 }, outside = { 448, 96, 480, 128 };
    RECT more = { 420, 100, 500, 340 }, ended;
    ULONGLONG now = 1000;

    ok(VideoTrackerReset(&tracker, 1280, 720), "VideoTrackerReset failed\n");
    ok(!Play(&tracker, &now, VIDEO_DETECT_MS - 2 * FRAME_MS, &video), "a region was found before %u ms\n",
       VIDEO_DETECT_MS);
    ok(Play(&tracker, &now, 4 * FRAME_MS, &video), "a region changing for %u ms was not found\n", VIDEO_DETECT_MS);
    ok(SameRect(&tracker.Region, 96, 96, 448, 352), "region (%ld,%ld)-(%ld,%ld), expected the tiles it covers\n",
       tracker.Region.left, tracker.Region.top, tracker.Region.right, tracker.Region.bottom);
    ok(VideoTrackerCovers(&tracker, &inside), "a tile inside the region is not covered\n");
    ok(!VideoTrackerCovers(&tracker, &outside), "a tile outside the region is covered\n");

    ok(Play(&tracker, &now, VIDEO_DETECT_MS + 4 * FRAME_MS, &more), "the region ended while it kept changing\n");
    ok(SameRect(&tracker.Region, 96, 96, 512, 352), "region (%ld,%ld)-(%ld,%ld) did not grow over the next tiles\n",
       tracker.Region.left, tracker.Region.top, tracker.Region.right, tracker.Region.bottom);

    now += VIDEO_IDLE_MS - FRAME_MS - 1;
    ok(VideoTrackerUpdate(&tracker, now, &ended), "the region ended before %u ms without change\n", VIDEO_IDLE_MS);
    ok(IsRectEmpty(&ended), "an active region reported an end\n");
    now += 1;
    ok(!VideoTrackerUpdate(&tracker, now, &ended), "the region did not end after %u ms without change\n",
       VIDEO_IDLE_MS);
    ok(SameRect(&ended, 96, 96, 512, 352), "the ended region is (%ld,%ld)-(%ld,%ld)\n", ended.left, ended.top,
       ended.right, ended.bottom);
    ok(!VideoTrackerCovers(&tracker, &inside), "an ended region still covers its tiles\n");

    ok(Play(&tracker, &now, VIDEO_DETECT_MS + 2 * FRAME_MS, &video), "the region was not found again\n");
    VideoTrackerStop(&tracker, &ended);
    ok(SameRect(&ended, 96, 96, 448, 352), "stopping reported (%ld,%ld)-(%ld,%ld)\n", ended.left, ended.top,
       ended.right, ended.bottom);
    ok(!tracker.Active, "the region is still active after a stop\n");
    VideoTrackerFree(&tracker);
}

static void TestNotVideo(void)
{
    VIDEO_TRACKER tracker = {0};
    RECT caret = { 300, 300, 302, 320 }, area = { 0, 0, 100, 60 }, ended;
    ULONGLONG now = 5000, end;
    UINT32 frame;
    BOOL active = FALSE;

    ok(VideoTrackerReset(&tracker, 1280, 720), "VideoTrackerReset failed\n");
    for (end = now + 6000; now < end; now += 530)
    {
        VideoTrackerChanged(&tracker, now, &caret);
        active |= VideoTrackerUpdate(&tracker, now, &ended);
    }
    ok(!active, "a blinking caret was sent as video\n");

    ok(!Play(&tracker, &now, 3 * VIDEO_DETECT_MS, &area), "an area of %u tiles was sent as video\n",
       VIDEO_MIN_TILES / 2);

    for (frame = 0, active = FALSE; frame < 6000 / FRAME_MS; ++frame, now += FRAME_MS)
    {
        LONG offset = (LONG)((frame % 40) < 20 ? (frame % 40) * 15 : (40 - frame % 40) * 15);
        RECT left = { 200 + offset, 200, 210 + offset, 500 }, right = { 700 + offset, 200, 710 + offset, 500 };
        VideoTrackerChanged(&tracker, now, &left);
        VideoTrackerChanged(&tracker, now, &right);
        active |= VideoTrackerUpdate(&tracker, now, &ended);
    }
    ok(!active, "the edges of a dragged window were sent as video\n");
    VideoTrackerFree(&tracker);
}

START_TEST(VideoDetect)
{
    TestVideo();
    TestNotVideo();
}
