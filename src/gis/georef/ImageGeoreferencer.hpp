#pragma once

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QString>
#include <QTextStream>

#include <array>
#include <cmath>
#include <limits>
#include <optional>
#include <vector>

namespace gis {

struct ImageControlPoint {
    double pixelX = 0;
    double pixelY = 0;
    double worldX = 0;
    double worldY = 0;
    bool validation = false;

    QJsonObject toJson() const
    {
        return {{"pixelX", pixelX}, {"pixelY", pixelY}, {"worldX", worldX},
                {"worldY", worldY}, {"validation", validation}};
    }

    static ImageControlPoint fromJson(const QJsonObject& value)
    {
        return {value["pixelX"].toDouble(), value["pixelY"].toDouble(),
                value["worldX"].toDouble(), value["worldY"].toDouble(),
                value["validation"].toBool()};
    }
};

enum class ImageTransformMethod { Affine, Homography };

struct ImageTransformPoint {
    double x = 0;
    double y = 0;
};

struct ImageReprojectionError {
    int controlPoint = -1;
    double pixelError = 0;
    double worldError = 0;
    bool validation = false;
};

struct ImageGeoreferenceResult {
    bool success = false;
    QString error;
    ImageTransformMethod method = ImageTransformMethod::Affine;
    std::array<double, 9> pixelToWorld {1, 0, 0, 0, 1, 0, 0, 0, 1};
    std::array<double, 9> worldToPixel {1, 0, 0, 0, 1, 0, 0, 0, 1};
    std::vector<ImageReprojectionError> errors;
    double trainingRmsePixels = 0;
    double trainingRmseWorld = 0;
    double validationRmsePixels = -1;
    double validationRmseWorld = -1;
};

class ImageGeoreferencer {
public:
    static ImageGeoreferenceResult fit(const std::vector<ImageControlPoint>& points,
                                       ImageTransformMethod method)
    {
        ImageGeoreferenceResult result;
        result.method = method;
        const int required = method == ImageTransformMethod::Affine ? 3 : 4;
        int trainingCount = 0;
        for (const auto& point : points) if (!point.validation) trainingCount++;
        if (trainingCount < required) {
            result.error = QString("%1 control points are required")
                .arg(required);
            return result;
        }

        bool solved = method == ImageTransformMethod::Affine
            ? fitAffine(points, result.pixelToWorld)
            : fitHomography(points, result.pixelToWorld);
        if (!solved) {
            result.error = "Control points are degenerate or nearly collinear";
            return result;
        }
        if (!invert(result.pixelToWorld, result.worldToPixel)) {
            result.error = "Calculated image transformation is singular";
            return result;
        }

        double trainPixelSquared = 0;
        double trainWorldSquared = 0;
        double validationPixelSquared = 0;
        double validationWorldSquared = 0;
        int validationCount = 0;
        for (int i = 0; i < int(points.size()); ++i) {
            const auto& point = points[i];
            const auto world = map(result.pixelToWorld, point.pixelX, point.pixelY);
            const auto pixel = map(result.worldToPixel, point.worldX, point.worldY);
            const double worldError = std::hypot(world.x - point.worldX, world.y - point.worldY);
            const double pixelError = std::hypot(pixel.x - point.pixelX, pixel.y - point.pixelY);
            result.errors.push_back({i, pixelError, worldError, point.validation});
            if (point.validation) {
                validationPixelSquared += pixelError * pixelError;
                validationWorldSquared += worldError * worldError;
                validationCount++;
            } else {
                trainPixelSquared += pixelError * pixelError;
                trainWorldSquared += worldError * worldError;
            }
        }
        result.trainingRmsePixels = std::sqrt(trainPixelSquared / trainingCount);
        result.trainingRmseWorld = std::sqrt(trainWorldSquared / trainingCount);
        if (validationCount > 0) {
            result.validationRmsePixels = std::sqrt(validationPixelSquared / validationCount);
            result.validationRmseWorld = std::sqrt(validationWorldSquared / validationCount);
        }
        result.success = true;
        return result;
    }

    static ImageTransformPoint pixelToWorld(const ImageGeoreferenceResult& result,
                                             double pixelX, double pixelY)
    {
        return map(result.pixelToWorld, pixelX, pixelY);
    }

    static ImageTransformPoint worldToPixel(const ImageGeoreferenceResult& result,
                                             double worldX, double worldY)
    {
        return map(result.worldToPixel, worldX, worldY);
    }

    static std::array<double, 6> worldFileCoefficients(const ImageGeoreferenceResult& result)
    {
        return {result.pixelToWorld[0], result.pixelToWorld[3], result.pixelToWorld[1],
                result.pixelToWorld[4], result.pixelToWorld[2], result.pixelToWorld[5]};
    }

    static QJsonObject toJson(const ImageGeoreferenceResult& result,
                              const std::vector<ImageControlPoint>& points,
                              const QString& crs)
    {
        QJsonArray matrix;
        for (double value : result.pixelToWorld) matrix.append(value);
        QJsonArray inverse;
        for (double value : result.worldToPixel) inverse.append(value);
        QJsonArray controls;
        for (const auto& point : points) controls.append(point.toJson());
        return {{"method", result.method == ImageTransformMethod::Affine ? "affine" : "homography"},
                {"crs", crs}, {"pixelToWorld", matrix}, {"worldToPixel", inverse},
                {"controlPoints", controls}, {"trainingRmsePixels", result.trainingRmsePixels},
                {"trainingRmseWorld", result.trainingRmseWorld},
                {"validationRmsePixels", result.validationRmsePixels},
                {"validationRmseWorld", result.validationRmseWorld}};
    }

    static bool fromJson(const QJsonObject& object, ImageGeoreferenceResult& result,
                         std::vector<ImageControlPoint>& points, QString& crs,
                         QString* error = nullptr)
    {
        const QJsonArray matrix = object["pixelToWorld"].toArray();
        const QJsonArray inverse = object["worldToPixel"].toArray();
        if (matrix.size() != 9 || inverse.size() != 9) {
            if (error) *error = "Georeference matrices must contain nine values";
            return false;
        }
        result = {};
        result.method = object["method"].toString() == "homography"
            ? ImageTransformMethod::Homography : ImageTransformMethod::Affine;
        for (int i = 0; i < 9; ++i) {
            result.pixelToWorld[i] = matrix[i].toDouble();
            result.worldToPixel[i] = inverse[i].toDouble();
        }
        result.trainingRmsePixels = object["trainingRmsePixels"].toDouble();
        result.trainingRmseWorld = object["trainingRmseWorld"].toDouble();
        result.validationRmsePixels = object["validationRmsePixels"].toDouble(-1);
        result.validationRmseWorld = object["validationRmseWorld"].toDouble(-1);
        result.success = true;
        crs = object["crs"].toString();
        points.clear();
        for (const auto& value : object["controlPoints"].toArray())
            points.push_back(ImageControlPoint::fromJson(value.toObject()));
        return true;
    }

    static bool save(const QString& path, const ImageGeoreferenceResult& result,
                     const std::vector<ImageControlPoint>& points, const QString& crs,
                     QString* error = nullptr)
    {
        QFile file(path);
        if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
            if (error) *error = QString("Cannot write georeference file: %1").arg(path);
            return false;
        }
        file.write(QJsonDocument(toJson(result, points, crs)).toJson(QJsonDocument::Indented));
        return true;
    }

    static bool load(const QString& path, ImageGeoreferenceResult& result,
                     std::vector<ImageControlPoint>& points, QString& crs,
                     QString* error = nullptr)
    {
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
            if (error) *error = QString("Cannot read georeference file: %1").arg(path);
            return false;
        }
        QJsonParseError parseError;
        const auto document = QJsonDocument::fromJson(file.readAll(), &parseError);
        if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
            if (error) *error = parseError.errorString();
            return false;
        }
        return fromJson(document.object(), result, points, crs, error);
    }

    static bool writeWorldFile(const QString& path, const ImageGeoreferenceResult& result,
                               QString* error = nullptr)
    {
        if (!result.success || result.method != ImageTransformMethod::Affine ||
            std::abs(result.pixelToWorld[6]) > 1e-12 ||
            std::abs(result.pixelToWorld[7]) > 1e-12) {
            if (error) *error = "World files support affine transformations only";
            return false;
        }
        QFile file(path);
        if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
            if (error) *error = QString("Cannot write world file: %1").arg(path);
            return false;
        }
        const auto coefficients = worldFileCoefficients(result);
        QTextStream stream(&file);
        stream.setRealNumberNotation(QTextStream::FixedNotation);
        stream.setRealNumberPrecision(12);
        for (double value : coefficients) stream << value << "\r\n";
        return true;
    }

private:
    template<size_t N>
    static bool solve(std::array<std::array<double, N>, N> matrix,
                      std::array<double, N> rhs, std::array<double, N>& solution)
    {
        for (size_t column = 0; column < N; ++column) {
            size_t pivot = column;
            for (size_t row = column + 1; row < N; ++row)
                if (std::abs(matrix[row][column]) > std::abs(matrix[pivot][column])) pivot = row;
            if (std::abs(matrix[pivot][column]) < 1e-12) return false;
            std::swap(matrix[column], matrix[pivot]);
            std::swap(rhs[column], rhs[pivot]);
            const double divisor = matrix[column][column];
            for (size_t j = column; j < N; ++j) matrix[column][j] /= divisor;
            rhs[column] /= divisor;
            for (size_t row = 0; row < N; ++row) {
                if (row == column) continue;
                const double factor = matrix[row][column];
                for (size_t j = column; j < N; ++j)
                    matrix[row][j] -= factor * matrix[column][j];
                rhs[row] -= factor * rhs[column];
            }
        }
        solution = rhs;
        return true;
    }

    template<size_t N>
    static void accumulate(const std::array<double, N>& row, double value,
                           std::array<std::array<double, N>, N>& normal,
                           std::array<double, N>& rhs)
    {
        for (size_t i = 0; i < N; ++i) {
            rhs[i] += row[i] * value;
            for (size_t j = 0; j < N; ++j) normal[i][j] += row[i] * row[j];
        }
    }

    static bool fitAffine(const std::vector<ImageControlPoint>& points,
                          std::array<double, 9>& transform)
    {
        std::array<std::array<double, 6>, 6> normal {};
        std::array<double, 6> rhs {};
        for (const auto& point : points) {
            if (point.validation) continue;
            accumulate<6>({point.pixelX, point.pixelY, 1, 0, 0, 0}, point.worldX, normal, rhs);
            accumulate<6>({0, 0, 0, point.pixelX, point.pixelY, 1}, point.worldY, normal, rhs);
        }
        std::array<double, 6> values {};
        if (!solve<6>(normal, rhs, values)) return false;
        transform = {values[0], values[1], values[2], values[3], values[4], values[5], 0, 0, 1};
        return true;
    }

    static bool fitHomography(const std::vector<ImageControlPoint>& points,
                              std::array<double, 9>& transform)
    {
        std::array<std::array<double, 8>, 8> normal {};
        std::array<double, 8> rhs {};
        for (const auto& point : points) {
            if (point.validation) continue;
            const double u = point.pixelX;
            const double v = point.pixelY;
            const double x = point.worldX;
            const double y = point.worldY;
            accumulate<8>({u, v, 1, 0, 0, 0, -x * u, -x * v}, x, normal, rhs);
            accumulate<8>({0, 0, 0, u, v, 1, -y * u, -y * v}, y, normal, rhs);
        }
        std::array<double, 8> values {};
        if (!solve<8>(normal, rhs, values)) return false;
        transform = {values[0], values[1], values[2], values[3], values[4], values[5],
                     values[6], values[7], 1};
        return true;
    }

    static bool invert(const std::array<double, 9>& matrix, std::array<double, 9>& inverse)
    {
        const double determinant =
            matrix[0] * (matrix[4] * matrix[8] - matrix[5] * matrix[7]) -
            matrix[1] * (matrix[3] * matrix[8] - matrix[5] * matrix[6]) +
            matrix[2] * (matrix[3] * matrix[7] - matrix[4] * matrix[6]);
        if (std::abs(determinant) < 1e-14) return false;
        inverse = {
            (matrix[4] * matrix[8] - matrix[5] * matrix[7]) / determinant,
            (matrix[2] * matrix[7] - matrix[1] * matrix[8]) / determinant,
            (matrix[1] * matrix[5] - matrix[2] * matrix[4]) / determinant,
            (matrix[5] * matrix[6] - matrix[3] * matrix[8]) / determinant,
            (matrix[0] * matrix[8] - matrix[2] * matrix[6]) / determinant,
            (matrix[2] * matrix[3] - matrix[0] * matrix[5]) / determinant,
            (matrix[3] * matrix[7] - matrix[4] * matrix[6]) / determinant,
            (matrix[1] * matrix[6] - matrix[0] * matrix[7]) / determinant,
            (matrix[0] * matrix[4] - matrix[1] * matrix[3]) / determinant
        };
        return true;
    }

    static ImageTransformPoint map(const std::array<double, 9>& matrix, double x, double y)
    {
        const double scale = matrix[6] * x + matrix[7] * y + matrix[8];
        if (std::abs(scale) < 1e-14)
            return {std::numeric_limits<double>::quiet_NaN(),
                    std::numeric_limits<double>::quiet_NaN()};
        return {(matrix[0] * x + matrix[1] * y + matrix[2]) / scale,
                (matrix[3] * x + matrix[4] * y + matrix[5]) / scale};
    }
};

} // namespace gis
