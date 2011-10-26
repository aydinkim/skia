/*
 * Copyright 2011 Google Inc.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "Test.h"
#include "SkAAClip.h"
#include "SkPath.h"

static void imoveTo(SkPath& path, int x, int y) {
    path.moveTo(SkIntToScalar(x), SkIntToScalar(y));
}

static void icubicTo(SkPath& path, int x0, int y0, int x1, int y1, int x2, int y2) {
    path.cubicTo(SkIntToScalar(x0), SkIntToScalar(y0),
                 SkIntToScalar(x1), SkIntToScalar(y1),
                 SkIntToScalar(x2), SkIntToScalar(y2));
}

static void test_trim_bounds(skiatest::Reporter* reporter) {
    SkPath path;
    SkAAClip clip;
    const int height = 40;
    const SkScalar sheight = SkIntToScalar(height);

    path.addOval(SkRect::MakeWH(sheight, sheight));
    REPORTER_ASSERT(reporter, sheight == path.getBounds().height());
    clip.setPath(path, NULL, true);
    REPORTER_ASSERT(reporter, height == clip.getBounds().height());

    // this is the trimmed height of this cubic (with aa). The critical thing
    // for this test is that it is less than height, which represents just
    // the bounds of the path's control-points.
    //
    // This used to fail until we tracked the MinY in the BuilderBlitter.
    //
    const int teardrop_height = 12;
    path.reset();
    imoveTo(path, 0, 20);
    icubicTo(path, 40, 40, 40, 0, 0, 20);
    REPORTER_ASSERT(reporter, sheight == path.getBounds().height());
    clip.setPath(path, NULL, true);
    REPORTER_ASSERT(reporter, teardrop_height == clip.getBounds().height());
}

static void TestAAClip(skiatest::Reporter* reporter) {
    test_trim_bounds(reporter);
}

#include "TestClassDef.h"
DEFINE_TESTCLASS("AAClip", AAClipTestClass, TestAAClip)