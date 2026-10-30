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
// File Name:   mdifaces.cpp
// Purpose:     Face detection and face embeddings (fork only, SpecMediaIntelligence.md)
/////////////////////////////////////////////////////////////////////////////
*/
#ifdef TECA_HAVE_OPENCV
// OpenCV first: its headers know nothing of Qt's keywords
#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>
#include <opencv2/objdetect/face.hpp>
#include <opencv2/core/utils/logger.hpp>
#endif

#include "core/mdifaces.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QImageReader>
#include <QDebug>
#include <algorithm>
#include <cmath>

namespace {

QString findModel(const QString &configuredPath, const QString &pattern)
{
    if (!configuredPath.isEmpty() && QFileInfo::exists(configuredPath))
        return configuredPath;
    QStringList dirs = {QDir::homePath() + QStringLiteral("/teca-models")};
    if (QCoreApplication::instance()) {
        const QString app = QCoreApplication::applicationDirPath();
        dirs << app + QStringLiteral("/models") << app + QStringLiteral("/../Resources/models")
             << app + QStringLiteral("/../share/teca/models");
    }
    for (const QString &d : dirs) {
        QFileInfoList found = QDir(d).entryInfoList({pattern}, QDir::Files, QDir::Name | QDir::Reversed);
        if (!found.isEmpty())
            return found.first().absoluteFilePath();   // the most recent model name first
    }
    return QString();
}

} // namespace

#ifdef TECA_HAVE_OPENCV
struct MdiFaces::Engine::Impl {
    cv::Ptr<cv::FaceDetectorYN>   detector;
    cv::Ptr<cv::FaceRecognizerSF> recognizer;
};
#else
struct MdiFaces::Engine::Impl {};
#endif

MdiFaces::Engine::Engine() : d(std::make_unique<Impl>()) {}
MdiFaces::Engine::~Engine() = default;

bool MdiFaces::compiledIn()
{
#ifdef TECA_HAVE_OPENCV
    return true;
#else
    return false;
#endif
}

QString MdiFaces::openCvVersion()
{
#ifdef TECA_HAVE_OPENCV
    return QString::fromLatin1(CV_VERSION);
#else
    return QString();
#endif
}

QString MdiFaces::findDetectorModel(const QString &configuredPath)
{
    return findModel(configuredPath, QStringLiteral("face_detection_yunet_*.onnx"));
}

QString MdiFaces::findRecognizerModel(const QString &configuredPath)
{
    return findModel(configuredPath, QStringLiteral("face_recognition_sface_*.onnx"));
}

QImage MdiFaces::readImage(const QString &path, QString *error)
{
    QImageReader reader(path);
    reader.setAutoTransform(true);   // EXIF orientation: faces upright
    if (!reader.canRead()) {
        *error = QStringLiteral("image format not readable");
        return QImage();
    }
    const QSize size = reader.size();
    if (size.isValid() && (size.width() > kMaxSide || size.height() > kMaxSide))
        reader.setScaledSize(size.scaled(kMaxSide, kMaxSide, Qt::KeepAspectRatio));
    QImage image = reader.read();
    if (image.isNull()) {
        *error = reader.errorString();
        return image;
    }
    if (image.width() > kMaxSide || image.height() > kMaxSide)
        image = image.scaled(kMaxSide, kMaxSide, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    return image;
}

bool MdiFaces::Engine::isLoaded() const
{
#ifdef TECA_HAVE_OPENCV
    return d->detector && d->recognizer;
#else
    return false;
#endif
}

bool MdiFaces::Engine::load(const QString &detectorModel, const QString &recognizerModel, QString *error)
{
#ifdef TECA_HAVE_OPENCV
    // OpenCV's notices (e.g. "Targets are not supported by the new graph
    // engine") are not the user's concern: errors only
    cv::utils::logging::setLogLevel(cv::utils::logging::LOG_LEVEL_ERROR);
    try {
        d->detector = cv::FaceDetectorYN::create(detectorModel.toStdString(), std::string(), cv::Size(320, 320),
                                                 kDetectScore, 0.3f, 5000);
        d->recognizer = cv::FaceRecognizerSF::create(recognizerModel.toStdString(), std::string());
    } catch (const cv::Exception &e) {
        d->detector.reset();
        d->recognizer.reset();
        *error = QString::fromStdString(e.msg).simplified();
        return false;
    }
    if (!isLoaded()) {
        *error = QStringLiteral("face models not loaded");
        return false;
    }
    return true;
#else
    Q_UNUSED(detectorModel)
    Q_UNUSED(recognizerModel)
    *error = QStringLiteral("opencv not available");
    return false;
#endif
}

QList<MdiFaces::Detection> MdiFaces::Engine::analyse(const QImage &image, QString *error)
{
    QList<Detection> found;
#ifdef TECA_HAVE_OPENCV
    if (!isLoaded() || image.isNull()) {
        *error = QStringLiteral("face models not loaded");
        return found;
    }
    const QImage rgb = image.convertToFormat(QImage::Format_RGB888);
    try {
        const cv::Mat wrapped(rgb.height(), rgb.width(), CV_8UC3, const_cast<uchar *>(rgb.constBits()),
                              size_t(rgb.bytesPerLine()));
        cv::Mat bgr;
        cv::cvtColor(wrapped, bgr, cv::COLOR_RGB2BGR);
        d->detector->setInputSize(bgr.size());
        cv::Mat faces;
        d->detector->detect(bgr, faces);
        const double W = bgr.cols, H = bgr.rows;
        for (int i = 0; i < faces.rows; ++i) {
            const float *row = faces.ptr<float>(i);
            const double x = row[0], y = row[1], w = row[2], h = row[3];
            if (std::min(w, h) < kMinFaceSide)
                continue;
            cv::Mat aligned, feature;
            d->recognizer->alignCrop(bgr, faces.row(i), aligned);
            d->recognizer->feature(aligned, feature);
            const cv::Mat f = feature.reshape(1, 1);
            Detection det;
            det.embedding.resize(int(f.total()));
            double norm = 0;
            for (int k = 0; k < det.embedding.size(); ++k) {
                det.embedding[k] = f.at<float>(0, k);
                norm += double(det.embedding[k]) * double(det.embedding[k]);
            }
            norm = std::sqrt(norm);
            if (norm <= 0)
                continue;
            for (float &v : det.embedding)
                v = float(v / norm);
            // Box in 0..1 of the image, clipped to it
            const double x0 = std::clamp(x, 0.0, W), y0 = std::clamp(y, 0.0, H);
            const double x1 = std::clamp(x + w, 0.0, W), y1 = std::clamp(y + h, 0.0, H);
            det.x = x0 / W;
            det.y = y0 / H;
            det.w = (x1 - x0) / W;
            det.h = (y1 - y0) / H;
            det.score = row[14];
            // A square crop around the face, with a margin
            const double side = std::max(w, h) * (1.0 + 2.0 * kCropMargin);
            const double cx = x + w / 2.0, cy = y + h / 2.0;
            const QRect box = QRect(int(std::round(cx - side / 2)), int(std::round(cy - side / 2)),
                                    int(std::round(side)), int(std::round(side))).intersected(image.rect());
            det.crop = image.copy(box).scaled(kCropSide, kCropSide, Qt::KeepAspectRatio, Qt::SmoothTransformation);
            found.append(det);
        }
    } catch (const cv::Exception &e) {
        *error = QString::fromStdString(e.msg).simplified();
        return {};
    }
    std::sort(found.begin(), found.end(), [](const Detection &a, const Detection &b) { return a.score > b.score; });
    if (found.size() > kMaxFaces)
        found = found.mid(0, kMaxFaces);
#else
    Q_UNUSED(image)
    *error = QStringLiteral("opencv not available");
#endif
    return found;
}
