/*
 * Copyright 2013 Google Inc.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef SkFontConfigInterface_DEFINED
#define SkFontConfigInterface_DEFINED

#include "SkFontStyle.h"
#include "SkRefCnt.h"
#include "SkTypeface.h"

/**
 *  \class SkFontConfigInterface
 *
 *  Provides SkFontHost clients with access to fontconfig services. They will
 *  access the global instance found in RefGlobal().
 */
class SK_API SkFontConfigInterface : public SkRefCnt {
public:
    /**
     *  Returns the global SkFontConfigInterface instance, and if it is not
     *  NULL, calls ref() on it. The caller must balance this with a call to
     *  unref().
     */
    static SkFontConfigInterface* RefGlobal();

    /**
     *  Replace the current global instance with the specified one, safely
     *  ref'ing the new instance, and unref'ing the previous. Returns its
     *  parameter (the new global instance).
     */
    static SkFontConfigInterface* SetGlobal(SkFontConfigInterface*);

    /**
     *  This should be treated as private to the impl of SkFontConfigInterface.
     *  Callers should not change or expect any particular values. It is meant
     *  to be a union of possible storage types to aid the impl.
     */
    struct FontIdentity {
        FontIdentity() : fID(0), fTTCIndex(0) {}

        bool operator==(const FontIdentity& other) const {
            return fID == other.fID &&
                   fTTCIndex == other.fTTCIndex &&
                   fString == other.fString;
        }

        uint32_t    fID;
        int32_t     fTTCIndex;
        SkString    fString;
        SkFontStyle fStyle;
    };

    /**
     *  Given a familyName and style, find the best match.
     *
     *  If a match is found, return true and set its outFontIdentifier.
     *      If outFamilyName is not null, assign the found familyName to it
     *          (which may differ from the requested familyName).
     *      If outStyle is not null, assign the found style to it
     *          (which may differ from the requested style).
     *
     *  If a match is not found, return false, and ignore all out parameters.
     */
    virtual bool matchFamilyName(const char familyName[],
                                 SkTypeface::Style requested,
                                 FontIdentity* outFontIdentifier,
                                 SkString* outFamilyName,
                                 SkTypeface::Style* outStyle) = 0;

    /**
     *  Given a FontRef, open a stream to access its data, or return null
     *  if the FontRef's data is not available. The caller is responsible for
     *  calling stream->unref() when it is done accessing the data.
     */
    virtual SkStream* openStream(const FontIdentity&) = 0;

    /**
     *  Return a singleton instance of a direct subclass that calls into
     *  libfontconfig. This does not affect the refcnt of the returned instance.
     */
    static SkFontConfigInterface* GetSingletonDirectInterface();

    // New APIS, which have default impls for now (which do nothing)

    virtual int countFamilies() { return 0; };
    virtual int getFamilySet(int index, SkString* outFamilyName,
                             FontIdentity outIdentities[], int maxCount) {
        return 0;
    }
    virtual int matchFamilySet(const char familyName[], SkString* outFamilyName,
                               FontIdentity outIdentities[], int maxCount) {
        return 0;
    }
};

#endif
