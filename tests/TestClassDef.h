
/*
 * Copyright 2011 Google Inc.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
/*  This file is meant to be included by .cpp files, so it can spew out a
    customized class + global definition.

    e.g.
    #include "TestClassDef.h"
    DEFINE_TESTCLASS("MyTest", MyTestClass, MyTestFunction)

    where MyTestFunction is declared as

        void MyTestFunction(skiatest::Reporter*)
*/

#define DEFINE_TESTCLASS(uiname, classname, function)                                   \
    namespace skiatest {                                                                \
        class classname : public Test {                                                 \
        public:                                                                         \
            static Test* Factory(void*) { return SkNEW(classname); }                    \
        protected:                                                                      \
            virtual void onGetName(SkString* name) SK_OVERRIDE { name->set(uiname); }   \
            virtual void onRun(Reporter* reporter) SK_OVERRIDE { function(reporter); }  \
        };                                                                              \
        static TestRegistry gReg_##classname(classname::Factory);                       \
    }

#define DEFINE_GPUTESTCLASS(uiname, classname, function)                                \
    namespace skiatest {                                                                \
        class classname : public GpuTest {                                              \
        public:                                                                         \
            static Test* Factory(void*) { return SkNEW(classname); }                    \
        protected:                                                                      \
            virtual void onGetName(SkString* name) SK_OVERRIDE { name->set(uiname); }   \
            virtual void onRun(Reporter* reporter) SK_OVERRIDE {                        \
                function(reporter, GetGrContextFactory());                              \
            }                                                                           \
        };                                                                              \
        static TestRegistry gReg_##classname(classname::Factory);                       \
    }
