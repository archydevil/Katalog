/*LICENCE
    This file is part of Katalog

    Copyright (C) 2020, the Katalog Development team

    Author: Stephane Couturier (Symbioxy)

    Katalog is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    Katalog is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with Katalog; if not, write to the Free Software
    Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
*/
/*FILE DESCRIPTION
/////////////////////////////////////////////////////////////////////////////
// Application: Katalog
// File Name:   mdifaces.h
// Purpose:     Face detection and face embeddings (fork only, SpecMediaIntelligence.md)
// Description: Local, with OpenCV (owner's answer 2026-10-30, "OpenCV
//              locale"): YuNet finds the faces of an image, SFace aligns each
//              face and turns it into a 128-number vector; two faces of the
//              same person have vectors close by cosine. Nothing leaves the
//              computer. Compiled only when CMake finds OpenCV
//              (TECA_HAVE_OPENCV); without it compiledIn() is false and the
//              "faces" analysis is skipped with its reason.
/////////////////////////////////////////////////////////////////////////////
*/
#ifndef MDIFACES_H
#define MDIFACES_H

#include <QString>
#include <QList>
#include <QVector>
#include <QImage>
#include <memory>

class MdiFaces
{
public:
    // Images are read at most this large (longest side) for detection
    static constexpr int    kMaxSide       = 1600;
    // Faces smaller than this (pixels, at that size) give unreliable vectors
    static constexpr int    kMinFaceSide   = 32;
    static constexpr float  kDetectScore   = 0.8f;
    static constexpr int    kMaxFaces      = 60;
    // Stored face crop: longest side, with a margin around the face
    static constexpr int    kCropSide      = 160;
    static constexpr double kCropMargin    = 0.25;

    struct Detection {
        double         x = 0, y = 0, w = 0, h = 0;   // 0..1 of the image
        double         score = 0;
        QVector<float> embedding;                    // unit length (128)
        QImage         crop;                         // kCropSide, for the face list
    };

    static bool    compiledIn();
    static QString openCvVersion();   // empty when not compiled in

    // ~/teca-models (or a configured file): face_detection_yunet_*.onnx and
    // face_recognition_sface_*.onnx; empty when absent
    static QString findDetectorModel(const QString &configuredPath = QString());
    static QString findRecognizerModel(const QString &configuredPath = QString());

    // One engine per thread (the networks are not shared between threads)
    class Engine
    {
    public:
        Engine();
        ~Engine();
        bool load(const QString &detectorModel, const QString &recognizerModel, QString *error);
        bool isLoaded() const;
        // Faces of an image already oriented (EXIF applied); empty when none
        QList<Detection> analyse(const QImage &image, QString *error);
    private:
        struct Impl;
        std::unique_ptr<Impl> d;
    };

    // Reads an image with its EXIF orientation, at most kMaxSide; null when unreadable
    static QImage readImage(const QString &path, QString *error);
};

#endif // MDIFACES_H
