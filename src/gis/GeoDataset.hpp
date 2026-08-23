#pragma once

// ============================================================
// GeoDataset.hpp — Geospatial dataset with mandatory CRS
//
// INVARIANT: No geospatial dataset enters the World Model
// without a known CRS. This struct enforces that by requiring
// a valid CRSDefinition at construction time.
// ============================================================

#include "crs/CRS.hpp"

#include <QString>
#include <vector>
#include <optional>
#include <string>

namespace gis {

enum class DatasetType {
    Raster,
    Vector,
    PointCloud,
    Unknown
};

struct GeoDataset {
    // Source CRS (the CRS the data was originally in)
    CRSDefinition sourceCRS;

    // Project CRS (the CRS the data has been transformed to, if any)
    // If empty, the data is still in sourceCRS
    std::optional<CRSDefinition> projectCRS;

    // Whether the data has been reprojected to the project CRS
    bool reprojected = false;

    // Dataset metadata
    QString name;
    QString filePath;
    DatasetType type = DatasetType::Unknown;
    BoundingBox bounds;     // bounds in sourceCRS

    // ── CRS Invariant ──────────────────────────────────────
    // A dataset is only valid if it has a known CRS.
    // Unknown-CRS data must NOT enter the World Model.
    bool hasValidCRS() const {
        return sourceCRS.isValid();
    }

    // Check if the dataset is ready for the World Model
    bool isWorldModelReady() const {
        if (!hasValidCRS()) return false;
        if (!reprojected) return false;
        if (!projectCRS.has_value()) return false;
        return projectCRS->isValid();
    }

    // Get the effective CRS (project CRS if reprojected, source CRS otherwise)
    const CRSDefinition& effectiveCRS() const {
        if (reprojected && projectCRS.has_value()) {
            return *projectCRS;
        }
        return sourceCRS;
    }

    // ── Factory: create with CRS ───────────────────────────
    static GeoDataset createWithCRS(
        const CRSDefinition& crs,
        const QString& name,
        const QString& filePath,
        DatasetType type) {
        GeoDataset ds;
        ds.sourceCRS = crs;
        ds.name = name;
        ds.filePath = filePath;
        ds.type = type;
        return ds;
    }

    // ── Factory: reject unknown CRS ────────────────────────
    // Returns nullopt if the CRS is invalid, enforcing the invariant.
    static std::optional<GeoDataset> createWithCRSStrict(
        const CRSDefinition& crs,
        const QString& name,
        const QString& filePath,
        DatasetType type) {
        if (!crs.isValid()) return std::nullopt;
        return createWithCRS(crs, name, filePath, type);
    }
};

// ── CRS Assignment Result ─────────────────────────────────
enum class CRSAssignmentResult {
    Success,
    InvalidCRS,
    AlreadyAssigned,
    TransformFailed
};

struct CRSAssignmentReport {
    CRSAssignmentResult result = CRSAssignmentResult::Success;
    QString errorMessage;
};

// Assign a CRS to a dataset that has no CRS (user-assigned)
inline CRSAssignmentReport assignCRSToDataset(
    GeoDataset& dataset,
    const CRSDefinition& crs) {
    CRSAssignmentReport report;

    if (!crs.isValid()) {
        report.result = CRSAssignmentResult::InvalidCRS;
        report.errorMessage = "Cannot assign an invalid CRS to a dataset.";
        return report;
    }

    if (dataset.hasValidCRS()) {
        report.result = CRSAssignmentResult::AlreadyAssigned;
        report.errorMessage = "Dataset already has a CRS. Use reprojection instead.";
        return report;
    }

    dataset.sourceCRS = crs;
    return report;
}

} // namespace gis
